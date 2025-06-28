#include "memory/memory_pool.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <new>
#include <cassert>

MemoryPool::MemoryPool(const Config& config)
    : config_(config)
{
    /* === 验证配置参数 === */
    assert(config_.small_block_size > 0);
    assert(config_.medium_block_size > config_.small_block_size);
    assert(config_.initial_pool_size > 0);
    assert(config_.max_pool_size >= config_.initial_pool_size);
    assert((config_.alignment & (config_.alignment - 1)) == 0);

    /* === 创建分层池 === */
    // 小块池：64B 块，每个 chunk 包含 256 个块 = 16 KB
    small_pool_ = std::make_unique<LayeredPool>(64, 256);
    // 中块池：4KB 块，每个 chunk 包含64 个块 = 256 KB
    medium_pool_ = std::make_unique<LayeredPool>(4096, 64);
    // 大块池：64KB 块, 每个 chunk 包含16个块 = 1MB
    large_pool_ = std::make_unique<LayeredPool>(65536, 16);

    /* === 预分配初始内存 === */
    expandPool(small_pool_.get());
    expandPool(medium_pool_.get());
    expandPool(large_pool_.get());
}

MemoryPool::~MemoryPool()
{
    is_shutdown_.store(true);

    // 在调试模式下检查内存泄漏
    if(config_.enable_debug){
        std::lock_guard<std::mutex> lock(debug_mutex_);
        if(!allocated_pointers_.empty()){
            // 这里记录日志 or 断言，表示有内存泄漏
            // 此处暂时只清空集合
            allocated_pointers_.clear();
        }
    }
}

void* MemoryPool::allocate(size_t size, size_t alignment)
{
    if(is_shutdown_.load() || size == 0){
        return nullptr;
    }

    // 使用默认对齐
    size_t actual_alignment = alignment > 0 ? alignment : config_.alignment;
    size_t aligned_size = alignSize(size, actual_alignment);

    // void* ptr = nullptr;
    // bool from_pool = false;

    // // 根据大小选择合适的池
    // LayeredPool* pool = selectPool(aligned_size);
    // if(pool){
    //     ptr = allocateFromPool(pool, aligned_size);
    //     from_pool = (ptr != nullptr);
    // }

    // // 如果池分配失败，使用系统分配
    // if(!ptr){
    //     // 使用 aligned_alloc 确保对齐
    //     ptr = std::aligned_alloc(actual_alignment, aligned_size);
    //     if(!ptr){
    //         throw std::bad_alloc();
    //     }
    // }

    void* ptr = std::aligned_alloc(actual_alignment, aligned_size);
    if(!ptr){
        throw std::bad_alloc();
    }

    // 更新统计信息
    if(config_.enable_statistics){
        // updateStatistics(size, true, from_pool);
        updateStatistics(size, true, false); // false表示不是从池分配
    }

    // 调试模式下跟踪分配
    if(config_.enable_debug){
        debugTrackAllocation(ptr, size);
    }

    return ptr;
}

void MemoryPool::deallocate(void* ptr)
{
    if(!ptr || is_shutdown_.load()){
        return;
    }

    // 调试模式下跟踪释放
    if(config_.enable_debug){
        debugTrackDeallocation(ptr);
    }

    // 临时修复：统一使用系统释放
    std::free(ptr);

    // 更新统计信息
    if(config_.enable_statistics){
        stats_.free_count.fetch_add(1);
    }

    // // 尝试在各个池中查找并释放
    // bool found = false;
    // if(!found){
    //     std::lock_guard<std::mutex> lock(small_pool_->mutex);
    //     // 这里需要实现在池中查找并释放的逻辑
    //     // 为了简化，我们假设使用系统释放
    // }

    // // 如果在池中没有找到，使用系统释放
    // if(!found){
    //     std::free(ptr);
    // }

    // // 更新统计信息
    // if(config_.enable_statistics){
    //     stats_.free_count.fetch_add(1);
    //     // 这里应该减少 current_usage, 但仍需要知道原始大小
    //     // 在实际实现中，可以在分配时存储元数据
    // }
}

void MemoryPool::defragment()
{
    // 对每个池进行碎片整理
    for(auto* pool : {small_pool_.get(), medium_pool_.get(), large_pool_.get()}){
        std::lock_guard<std::mutex> lock(pool->mutex);

        // 合并相邻的空闲块
        MemoryBlock* current = pool->free_list;
        while(current && current->next){
            MemoryBlock* next = current->next;

            // 检查是否相邻
            if(static_cast<uint8_t*>(current->data) + current->size == next->data){
                // 合并块
                current->size += next->size;
                current->next = next->next;
                if(next->next){
                    next->next->prev = current;
                }
                delete next;
            }else{
                current = current->next;
            }
        }
    }
}

bool MemoryPool::isHealthy() const
{
    // 检查基本的健康状态
    size_t current = stats_.current_usage.load();
    size_t peak = stats_.peak_usage.load();

    // 检查是否有异常的内存使用模式
    if(current > config_.max_pool_size){
        return false;
    }

    // 检查碎片率是否过高
    if(getStatistics().getFragmentationRate() > 0.5){
        return false;
    }

    return true;
}

void MemoryPool::resetStatistics()
{
    stats_.total_allocated.store(0);
    stats_.total_freed.store(0);
    stats_.current_usage.store(0);
    stats_.peak_usage.store(0);
    stats_.allocation_count.store(0);
    stats_.free_count.store(0);
    stats_.pool_hit_count.store(0);
    stats_.system_alloc_count.store(0);
}

std::string MemoryPool::getReport() const
{
    auto stats = getStatistics();
    std::ostringstream oss;

    oss << " === Memory Pool Report ===\n";
    oss << "Current Usage: " << stats.current_usage << " bytes\n";
    oss << "Peak Usage: " << stats.peak_usage << " bytes\n";
    oss << "Total Allocated: " << stats.total_allocated << " bytes\n";
    oss << "Total Freed: " << stats.total_freed << " bytes\n";
    oss << "Allocation Count: " << stats.allocation_count << "\n";
    oss << "Free Count: " << stats.free_count << "\n";
    oss << "Pool Hit Rate: " << std::fixed << std::setprecision(2) << (stats.getHitRate() * 100) << "%\n";
    oss << "Fragmentation Rate: " << std::fixed << std::setprecision(2) << (stats.getFragmentationRate() * 100) << "%\n";
    oss << "System Allocations: " << stats.system_alloc_count << "\n";

    return oss.str();
}

void* MemoryPool::allocateFromPool(LayeredPool* pool, size_t size)
{
    std::lock_guard<std::mutex> lock(pool->mutex);

    // 查找合适的空闲块
    MemoryBlock* block = pool->free_list;
    while(block){
        if(block->is_free && block->size >= size){
            block->is_free = false;

            // 如果块太大，考虑切割（简化实现中省略）

            // 从空闲链表中移除
            if(block->prev){
                block->prev->next = block->next;
            }else{
                pool->free_list = block->next;
            }
            if(block->next){
                block->next->prev = block->prev;
            }

            return block->data;
        }
        block = block->next;
    }

    // 没有合适的空闲块，尝试扩展池
    if(expandPool(pool)){
        return allocateFromPool(pool, size);    // 递归调用
    }

    return nullptr;
}

bool MemoryPool::expandPool(LayeredPool* pool)
{
    // 计算需要分配的内存大小
    size_t chunk_size = pool->block_size * pool->blocks_per_chunk;

    // 检查是否超过最大池大小限制
    size_t current_total = stats_.current_usage.load();
    if(current_total + chunk_size > config_.max_pool_size){
        return false;
    }

    // 分配大块内存 - 修复：使用数组版本的unique_ptr
    auto chunk = std::unique_ptr<uint8_t[]>(new uint8_t[chunk_size]);
    if(!chunk){
        return false;
    }

    uint8_t* chunk_ptr = chunk.get();

    // 将大块内存划分为小块并添加到空闲链表
    for(size_t i = 0; i < pool->blocks_per_chunk; ++i){
        void* block_data = chunk_ptr + i * pool->block_size;

        // 创建内存块描述符
        auto* block = new MemoryBlock(block_data, pool->block_size);

        // 添加到空闲链表头部
        block->next = pool->free_list;
        if(pool->free_list){
            pool->free_list->prev = block;
        }
        pool->free_list = block;
    }

    // 保存 chunk 指针以便后续释放
    pool->chunks.push_back(std::move(chunk));

    return true;
}

void* MemoryPool::alignPointer(void* ptr, size_t alignment)
{
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t aligned_addr = (addr + alignment - 1) & ~(alignment - 1);
    return reinterpret_cast<void*>(aligned_addr);
}

size_t MemoryPool::alignSize(size_t size, size_t alignment)
{
    return (size + alignment - 1) & ~(alignment - 1);
}

void MemoryPool::updateStatistics(size_t size, bool is_allocation, bool from_pool)
{
    if(is_allocation){
        stats_.allocation_count.fetch_add(1);
        stats_.total_allocated.fetch_add(size);

        size_t old_usage = stats_.current_usage.fetch_add(size);
        size_t new_usage = old_usage + size;

        // 更新峰值使用量
        size_t old_peak = stats_.peak_usage.load();
        while(new_usage > old_peak
               && !stats_.peak_usage.compare_exchange_weak(old_peak, new_usage)){
            // 循环直到更新峰值
        }

        if(from_pool){
            stats_.pool_hit_count.fetch_add(1);
        }else{
            stats_.system_alloc_count.fetch_add(1);
        }
    }else{
        stats_.free_count.fetch_add(1);
        stats_.total_freed.fetch_add(size);
        stats_.current_usage.fetch_sub(size);
    }
}

void MemoryPool::debugTrackAllocation(void* ptr, size_t size)
{
    if (!config_.enable_debug) return;
    
    std::lock_guard<std::mutex> lock(debug_mutex_);
    allocated_pointers_.insert(ptr);
}

void MemoryPool::debugTrackDeallocation(void* ptr)
{
    if (!config_.enable_debug) return;
    
    std::lock_guard<std::mutex> lock(debug_mutex_);
    allocated_pointers_.erase(ptr);
}

MemoryPool::LayeredPool* MemoryPool::selectPool(size_t size)
{
    if(size <= config_.small_block_size){
        return small_pool_.get();
    }else if(size <= config_.medium_block_size){
        return medium_pool_.get();
    }else if(size <= large_pool_->block_size){
        return large_pool_.get();
    }

    return nullptr; // 超大块使用系统分配
}
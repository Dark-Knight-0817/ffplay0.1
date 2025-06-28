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
    // 小块池：1KB 块，每个 chunk 包含 256 个块 = 256 KB
    small_pool_ = std::make_unique<LayeredPool>(config_.small_block_size, 256);
    // 中块池：64KB 块，每个 chunk 包含64 个块 = 4 MB  
    medium_pool_ = std::make_unique<LayeredPool>(config_.medium_block_size, 64);
    // 大块池：1MB 块, 每个 chunk 包含16个块 = 16 MB
    large_pool_ = std::make_unique<LayeredPool>(config_.medium_block_size * 16, 16);

    /* === 预分配初始内存 === */
    expandPool(small_pool_.get());   // 预分配小块池
    // 中大块池按需分配，减少内存浪费
}

MemoryPool::~MemoryPool()
{
    is_shutdown_.store(true);

    // 清理分配记录
    {
        std::lock_guard<std::mutex> lock(pointer_mutex_);
        pointer_sources_.clear();
    }

    // 在调试模式下检查内存泄漏
    if(config_.enable_debug){
        std::lock_guard<std::mutex> lock(debug_mutex_);
        if(!allocated_pointers_.empty()){
            // 输出泄漏信息
            if(config_.enable_statistics) {
                fprintf(stderr, "Memory leak detected: %zu pointers not freed\n", 
                        allocated_pointers_.size());
            }
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

    void* ptr = nullptr;
    bool from_pool = false;

    // 根据大小选择合适的池
    LayeredPool* pool = selectPool(aligned_size);
    if(pool && aligned_size <= pool->block_size){
        ptr = allocateFromPool(pool, aligned_size);
        from_pool = (ptr != nullptr);
        
        // 如果从池分配成功，进行对齐调整
        if(ptr && actual_alignment > config_.alignment) {
            ptr = alignPointer(ptr, actual_alignment);
        }
    }

    // 如果池分配失败，使用系统分配
    if(!ptr){
        ptr = std::aligned_alloc(actual_alignment, aligned_size);
        if(!ptr){
            throw std::bad_alloc();
        }
        from_pool = false;
    }

    // 记录指针来源 - 使用更高效的方式
    recordPointerSource(ptr, from_pool, size);

    // 更新统计信息
    if(config_.enable_statistics){
        updateStatistics(size, true, from_pool);
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

    // 查找指针来源和大小
    bool from_pool = false;
    size_t original_size = 0;
    
    {
        std::lock_guard<std::mutex> lock(pointer_mutex_);
        auto it = pointer_sources_.find(ptr);
        if(it != pointer_sources_.end()){
            from_pool = it->second.first;
            original_size = it->second.second;
            pointer_sources_.erase(it);
        }
    }

    if(from_pool){
        // 池分配的内存，归还到对应的池
        deallocateToPool(ptr);
    } else {
        // 系统分配的内存，使用系统释放
        std::free(ptr);
    }

    // 更新统计信息
    if(config_.enable_statistics){
        updateStatistics(original_size, false, from_pool);
    }
}

void MemoryPool::defragment()
{
    // 对每个池进行碎片整理
    for(auto* pool : {small_pool_.get(), medium_pool_.get(), large_pool_.get()}){
        if(!pool) continue;
        
        std::lock_guard<std::mutex> lock(pool->mutex);

        // 合并相邻的空闲块
        MemoryBlock* current = pool->free_list;
        size_t merged_count = 0;
        
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
                merged_count++;
            }else{
                current = current->next;
            }
        }
        
        // 输出碎片整理信息
        if(config_.enable_debug && merged_count > 0) {
            printf("Pool defragmentation: merged %zu blocks\n", merged_count);
        }
    }
}

bool MemoryPool::isHealthy() const
{
    // 检查基本的健康状态
    size_t current = stats_.current_usage.load();

    // 检查是否有异常的内存使用模式
    if(current > config_.max_pool_size){
        return false;
    }

    // 检查碎片率是否过高
    double frag_rate = getStatistics().getFragmentationRate();
    if(frag_rate > 0.8){  // 80%碎片率警告
        return false;
    }

    // 检查各个池的健康状态
    for(auto* pool : {small_pool_.get(), medium_pool_.get(), large_pool_.get()}){
        if(!pool) continue;
        
        std::lock_guard<std::mutex> lock(pool->mutex);
        // 检查空闲链表完整性
        MemoryBlock* block = pool->free_list;
        size_t count = 0;
        while(block && count < 10000) {  // 防止无限循环
            if(!block->is_free) {
                return false;  // 空闲链表中有非空闲块
            }
            block = block->next;
            count++;
        }
        
        if(count >= 10000) {
            return false;  // 可能的循环链表
        }
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
    oss << "Current Usage: " << formatBytes(stats.current_usage) << "\n";
    oss << "Peak Usage: " << formatBytes(stats.peak_usage) << "\n";
    oss << "Total Allocated: " << formatBytes(stats.total_allocated) << "\n";
    oss << "Total Freed: " << formatBytes(stats.total_freed) << "\n";
    oss << "Allocation Count: " << stats.allocation_count << "\n";
    oss << "Free Count: " << stats.free_count << "\n";
    oss << "Pool Hit Rate: " << std::fixed << std::setprecision(2) << (stats.getHitRate() * 100) << "%\n";
    oss << "Fragmentation Rate: " << std::fixed << std::setprecision(2) << (stats.getFragmentationRate() * 100) << "%\n";
    oss << "System Allocations: " << stats.system_alloc_count << "\n";
    
    // 添加池状态信息
    oss << "\n === Pool Status ===\n";
    oss << getPoolStatus();

    return oss.str();
}

void* MemoryPool::allocateFromPool(LayeredPool* pool, size_t size)
{
    std::lock_guard<std::mutex> lock(pool->mutex);

    // 快速路径：检查第一个空闲块
    if(pool->free_list && pool->free_list->is_free && pool->free_list->size >= size) {
        MemoryBlock* block = pool->free_list;
        block->is_free = false;
        
        // 从空闲链表移除
        pool->free_list = block->next;
        if(block->next) {
            block->next->prev = nullptr;
        }
        block->next = nullptr;
        block->prev = nullptr;
        
        return block->data;
    }

    // 慢速路径：查找合适的空闲块
    MemoryBlock* block = pool->free_list;
    while(block){
        if(block->is_free && block->size >= size){
            block->is_free = false;
            
            // 从空闲链表移除
            if(block->prev) {
                block->prev->next = block->next;
            } else {
                pool->free_list = block->next;
            }
            if(block->next) {
                block->next->prev = block->prev;
            }
            
            // 重置链表指针
            block->next = nullptr;
            block->prev = nullptr;
            
            return block->data;
        }
        block = block->next;
    }

    // 没有合适的空闲块，尝试扩展池
    if(expandPool(pool)){
        // 扩展成功，获取新的空闲块
        if(pool->free_list && pool->free_list->is_free){
            MemoryBlock* block = pool->free_list;
            block->is_free = false;
            pool->free_list = block->next;
            if(block->next) {
                block->next->prev = nullptr;
            }
            block->next = nullptr;
            block->prev = nullptr;
            return block->data;
        }
    }

    return nullptr;
}

void MemoryPool::deallocateToPool(void* ptr)
{
    // 使用更安全的方式查找池
    LayeredPool* target_pool = nullptr;
    MemoryBlock* reuse_block = nullptr;
    
    // 查找指针属于哪个池
    for(auto* pool : {small_pool_.get(), medium_pool_.get(), large_pool_.get()}){
        if(!pool) continue;
        
        // 使用try_lock避免死锁
        std::unique_lock<std::mutex> lock(pool->mutex, std::try_to_lock);
        if(!lock.owns_lock()) continue;
        
        // 检查指针是否在这个池的某个chunk中
        for(const auto& chunk : pool->chunks){
            uint8_t* chunk_start = chunk.get();
            uint8_t* chunk_end = chunk_start + (pool->block_size * pool->blocks_per_chunk);
            
            if(ptr >= chunk_start && ptr < chunk_end){
                target_pool = pool;
                
                // 计算正确的块起始地址
                size_t block_index = (static_cast<uint8_t*>(ptr) - chunk_start) / pool->block_size;
                void* block_start = chunk_start + block_index * pool->block_size;
                
                // 查找是否有现存的MemoryBlock可以重用
                MemoryBlock* existing = findMemoryBlock(pool, block_start);
                if(existing) {
                    existing->is_free = true;
                    // 添加到空闲链表头部
                    existing->next = pool->free_list;
                    existing->prev = nullptr;
                    if(pool->free_list) {
                        pool->free_list->prev = existing;
                    }
                    pool->free_list = existing;
                } else {
                    // 创建新的MemoryBlock
                    auto* new_block = new(std::nothrow) MemoryBlock(block_start, pool->block_size);
                    if(new_block) {
                        new_block->next = pool->free_list;
                        if(pool->free_list) {
                            pool->free_list->prev = new_block;
                        }
                        pool->free_list = new_block;
                    }
                }
                return;
            }
        }
    }
    
    // 如果没找到，用系统释放
    std::free(ptr);
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

    // 分配大块内存，使用对齐分配
    auto chunk = std::unique_ptr<uint8_t[]>(
        static_cast<uint8_t*>(std::aligned_alloc(config_.alignment, chunk_size))
    );
    
    if(!chunk){
        return false;
    }

    uint8_t* chunk_ptr = chunk.get();

    // 将大块内存划分为小块并添加到空闲链表
    for(size_t i = 0; i < pool->blocks_per_chunk; ++i){
        void* block_data = chunk_ptr + i * pool->block_size;

        // 创建内存块描述符
        auto* block = new(std::nothrow) MemoryBlock(block_data, pool->block_size);
        if(!block) {
            // 内存不足，清理已创建的块
            break;
        }

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
    if(!config_.enable_statistics) return;
    
    if(is_allocation){
        stats_.allocation_count.fetch_add(1);
        stats_.total_allocated.fetch_add(size);

        size_t old_usage = stats_.current_usage.fetch_add(size);
        size_t new_usage = old_usage + size;

        // 更新峰值使用量（使用更高效的原子操作）
        size_t old_peak = stats_.peak_usage.load(std::memory_order_relaxed);
        while(new_usage > old_peak) {
            if(stats_.peak_usage.compare_exchange_weak(old_peak, new_usage, 
                                                      std::memory_order_relaxed)){
                break;
            }
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

// 新增的辅助方法
void MemoryPool::recordPointerSource(void* ptr, bool from_pool, size_t size)
{
    std::lock_guard<std::mutex> lock(pointer_mutex_);
    pointer_sources_[ptr] = std::make_pair(from_pool, size);
}

MemoryPool::MemoryBlock* MemoryPool::findMemoryBlock(LayeredPool* pool, void* ptr)
{
    // 在已分配的块中查找（这里简化，实际需要维护已分配块列表）
    return nullptr;
}

std::string MemoryPool::formatBytes(size_t bytes) const
{
    const char* units[] = {"B", "KB", "MB", "GB"};
    double size = static_cast<double>(bytes);
    int unit = 0;
    
    while(size >= 1024.0 && unit < 3) {
        size /= 1024.0;
        unit++;
    }
    
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << size << " " << units[unit];
    return oss.str();
}

std::string MemoryPool::getPoolStatus() const
{
    std::ostringstream oss;
    
    auto printPoolInfo = [&](const char* name, const std::unique_ptr<LayeredPool>& pool) {
        if(!pool) return;
        
        std::lock_guard<std::mutex> lock(pool->mutex);
        size_t free_blocks = 0;
        MemoryBlock* block = pool->free_list;
        while(block) {
            if(block->is_free) free_blocks++;
            block = block->next;
        }
        
        oss << name << " Pool: " << pool->chunks.size() << " chunks, " 
            << free_blocks << " free blocks\n";
    };
    
    printPoolInfo("Small", small_pool_);
    printPoolInfo("Medium", medium_pool_);
    printPoolInfo("Large", large_pool_);
    
    return oss.str();
}
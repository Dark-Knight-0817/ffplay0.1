#include "memory/memory_pool.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <new>
#include <cassert>

// ============ 跨平台内存对齐兼容性代码 ============
#ifdef _WIN32
    #include <malloc.h>  // for _aligned_malloc and _aligned_free
#elif defined(__APPLE__) || defined(__linux__)
    #include <cstdlib>   // for aligned_alloc and free
#if !defined(__APPLE__)
    #include <malloc.h>  // for memalign on some Linux systems
#endif
#endif

namespace {
// 跨平台对齐内存分配函数
inline void* aligned_alloc_compat(size_t alignment, size_t size) {
    // 确保 size 是 alignment 的倍数（某些平台要求）
    size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);

#ifdef _WIN32
    // Windows: 使用 _aligned_malloc
    return _aligned_malloc(aligned_size, alignment);

#elif defined(__APPLE__)
// macOS: 优先使用 aligned_alloc (macOS 10.15+)
#if defined(MAC_OS_X_VERSION_10_15) && MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_15
    return std::aligned_alloc(alignment, aligned_size);
#else
        // 回退到 posix_memalign
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, aligned_size) == 0) {
        return ptr;
    }
    return nullptr;
#endif

#elif defined(__linux__)
// Linux: 检查 glibc 版本和 C++ 标准支持
#if defined(__GLIBC__) && __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 16 && __cplusplus >= 201703L
        // glibc 2.16+ 且 C++17+ 支持 aligned_alloc
    return std::aligned_alloc(alignment, aligned_size);
#else
        // 回退到 posix_memalign 或 memalign
    void* ptr = nullptr;
#ifdef _POSIX_C_SOURCE
    if (posix_memalign(&ptr, alignment, aligned_size) == 0) {
        return ptr;
    }
#else
        // 某些老版本 Linux 系统
    ptr = memalign(alignment, aligned_size);
    if (ptr != nullptr) {
        return ptr;
    }
#endif
    return nullptr;
#endif

#else
    // 其他 Unix 系统：通用实现
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, aligned_size) == 0) {
        return ptr;
    }

    // 最后的回退：手动对齐（兼容性最强但效率较低）
    ptr = std::malloc(aligned_size + alignment - 1 + sizeof(void*));
    if (!ptr) return nullptr;

    // 计算对齐地址
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
    uintptr_t aligned_addr = (addr + sizeof(void*) + alignment - 1) & ~(alignment - 1);

    // 在对齐地址前存储原始指针（用于释放）
    void** aligned_ptr = reinterpret_cast<void**>(aligned_addr);
    *(aligned_ptr - 1) = ptr;

    return reinterpret_cast<void*>(aligned_addr);
#endif
}

// 跨平台对齐内存释放函数
inline void aligned_free_compat(void* ptr) {
    if (!ptr) return;

#ifdef _WIN32
    _aligned_free(ptr);

#elif defined(__APPLE__) || defined(__linux__)
#if (defined(__APPLE__) && defined(MAC_OS_X_VERSION_10_15) && MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_15) || \
    (defined(__linux__) && defined(__GLIBC__) && __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 16 && __cplusplus >= 201703L)
        std::free(ptr);
#else
    std::free(ptr);  // posix_memalign 分配的内存用 free 释放
#endif

#else
    // 通用实现的释放：需要获取原始指针
    void** aligned_ptr = reinterpret_cast<void**>(ptr);
    void* original_ptr = *(aligned_ptr - 1);
    std::free(original_ptr);
#endif
}
}

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
    allocateChunk(small_pool_.get());   // 预分配小块池
    // 中大块池按需分配，减少内存浪费
}

MemoryPool::~MemoryPool()
{
    is_shutdown_.store(true);

    // 清理分配记录
    {
        std::lock_guard<std::mutex> lock(pointer_mutex_);
        pointer_sources_.clear();   // 清空内存路由
    }

    // 在调试模式下检查内存泄漏
    if(config_.enable_debug){
        std::lock_guard<std::mutex> lock(debug_mutex_);
        if(!allocated_pointers_.empty()){ // 检测到内存泄漏的指针没被释放
            // 输出泄漏信息
            if(config_.enable_statistics) {
                fprintf(stderr, "Memory leak detected: %zu pointers not freed\n", 
                        allocated_pointers_.size());
            }
            allocated_pointers_.clear();    // 清空set结构,内存由智能指针释放
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
        // ptr = std::aligned_alloc(actual_alignment, aligned_size);
        ptr = aligned_alloc_compat(actual_alignment, aligned_size);
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
        // std::free(ptr);
        aligned_free_compat(ptr);
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

    // 检查真正的碎片率是否过高
    double frag_rate = getFragmentationRate();
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
    auto health = getHealthReport();
    std::ostringstream oss;

    oss << " === Memory Pool Report ===\n";
    oss << "Memory Usage:\n";
    oss << "  Current Usage: " << formatBytes(stats.current_usage) << "\n";
    oss << "  Peak Usage: " << formatBytes(stats.peak_usage) << "\n";
    oss << "  Total Allocated: " << formatBytes(stats.total_allocated) << "\n";
    oss << "  Total Freed: " << formatBytes(stats.total_freed) << "\n";

    oss << "\nAllocation Statistics:\n";
    oss << "  Allocation Count: " << stats.allocation_count << "\n";
    oss << "  Free Count: " << stats.free_count << "\n";
    oss << "  Pool Hit Rate: " << std::fixed << std::setprecision(2) << (stats.getHitRate() * 100) << "%\n";
    oss << "  System Allocations: " << stats.system_alloc_count << "\n";

    oss << "\nMemory Health Analysis:\n";
    oss << "  Memory Utilization: " << std::fixed << std::setprecision(2) << (health.utilization_rate * 100) << "%\n";
    oss << "  Real Fragmentation Rate: " << std::fixed << std::setprecision(2) << (health.fragmentation_rate * 100) << "%\n";
    oss << "  Unused Memory Ratio: " << std::fixed << std::setprecision(2) << (health.unused_ratio * 100) << "%\n";

    oss << "\nFree Block Analysis:\n";
    oss << "  Total Free Blocks: " << health.total_free_blocks << "\n";
    oss << "  Largest Free Block: " << formatBytes(health.largest_free_block) << "\n";
    oss << "  Smallest Free Block: " << formatBytes(health.smallest_free_block) << "\n";
    oss << "  Average Free Block: " << formatBytes(health.average_free_block_size) << "\n";
    oss << "  Block Size Variance: " << std::fixed << std::setprecision(1) << health.free_block_size_variance << "\n";

    // 添加健康状态评估
    oss << "\nHealth Assessment:\n";
    if (health.fragmentation_rate < 0.2) {
        oss << "  Status: ✅ Excellent - Low fragmentation\n";
    } else if (health.fragmentation_rate < 0.5) {
        oss << "  Status: ⚠️  Good - Moderate fragmentation\n";
    } else if (health.fragmentation_rate < 0.8) {
        oss << "  Status: ⚠️  Warning - High fragmentation\n";
    } else {
        oss << "  Status: ❌ Critical - Severe fragmentation\n";
        oss << "  Recommendation: Consider calling defragment()\n";
    }

    oss << "\n === Pool Status ===\n";
    oss << getPoolStatus();

    return oss.str();
}

double MemoryPool::getFragmentationRate() const
{
    if (is_shutdown_.load()) return 0.0;

    size_t total_free_memory = 0;
    size_t largest_free_block = 0;

    // 分析所有池的碎片情况
    for (auto* pool : {small_pool_.get(), medium_pool_.get(), large_pool_.get()}) {
        if (!pool) continue;

        PoolFragmentInfo info = analyzePoolFragmentation(pool);
        
        if (info.total_free_memory > 0) {
            total_free_memory += info.total_free_memory;
            largest_free_block = std::max(largest_free_block, info.largest_free_block);
        }
    }

    if (total_free_memory == 0) {
        return 0.0; // 没有空闲内存，无碎片
    }

    // 碎片率 = 1 - (最大连续块 / 总空闲内存)
    // 值越接近1表示碎片化越严重
    double fragmentation = 1.0 - static_cast<double>(largest_free_block) / total_free_memory;
    
    // 确保返回值在合理范围内
    return std::max(0.0, std::min(1.0, fragmentation));
}

double MemoryPool::getMemoryUtilizationRate() const
{
    size_t peak = stats_.peak_usage.load();
    size_t current = stats_.current_usage.load();
    return peak > 0 ? static_cast<double>(current) / peak : 1.0;
}

MemoryPool::HealthReport MemoryPool::getHealthReport() const
{
    HealthReport report = {};

    if (is_shutdown_.load()) {
        return report;
    }

    std::vector<size_t> all_free_block_sizes;
    size_t total_free_memory = 0;
    size_t largest_free_block = 0;
    size_t smallest_free_block = SIZE_MAX;
    size_t total_free_blocks = 0;

    // 收集所有池的碎片信息
    for (auto* pool : {small_pool_.get(), medium_pool_.get(), large_pool_.get()}) {
        if (!pool) continue;

        PoolFragmentInfo info = analyzePoolFragmentation(pool);
        
        if (info.total_free_memory > 0) {
            total_free_memory += info.total_free_memory;
            total_free_blocks += info.free_block_count;
            largest_free_block = std::max(largest_free_block, info.largest_free_block);
            
            for (size_t size : info.free_block_sizes) {
                smallest_free_block = std::min(smallest_free_block, size);
                all_free_block_sizes.push_back(size);
            }
        }
    }

    // 填充报告
    report.fragmentation_rate = getFragmentationRate();
    report.utilization_rate = getMemoryUtilizationRate();
    report.unused_ratio = 1.0 - report.utilization_rate;
    report.total_free_blocks = total_free_blocks;
    report.largest_free_block = largest_free_block;
    report.smallest_free_block = (smallest_free_block == SIZE_MAX) ? 0 : smallest_free_block;

    // 计算平均空闲块大小
    if (total_free_blocks > 0) {
        report.average_free_block_size = total_free_memory / total_free_blocks;

        // 计算空闲块大小方差
        double sum_squared_diff = 0.0;
        double average = static_cast<double>(report.average_free_block_size);
        
        for (size_t size : all_free_block_sizes) {
            double diff = static_cast<double>(size) - average;
            sum_squared_diff += diff * diff;
        }
        
        report.free_block_size_variance = sum_squared_diff / total_free_blocks;
    }

    return report;
}

MemoryPool::PoolFragmentInfo MemoryPool::analyzePoolFragmentation(LayeredPool* pool) const
{
    PoolFragmentInfo info = {};
    
    if (!pool) return info;

    // 使用try_lock避免在统计时阻塞正常操作
    std::unique_lock<std::mutex> lock(pool->mutex, std::try_to_lock);
    if (!lock.owns_lock()) {
        // 无法获取锁，返回空信息
        return info;
    }

    // 遍历空闲链表
    MemoryBlock* block = pool->free_list;
    while (block) {
        if (block->is_free) {
            info.total_free_memory += block->size;
            info.largest_free_block = std::max(info.largest_free_block, block->size);
            info.free_block_count++;
            info.free_block_sizes.push_back(block->size);
        }
        block = block->next;
    }

    return info;
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
    if(allocateChunk(pool)){
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

bool MemoryPool::allocateChunk(LayeredPool* pool)
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
        // static_cast<uint8_t*>(std::aligned_alloc(config_.alignment, chunk_size))
        static_cast<uint8_t*>(aligned_alloc_compat(config_.alignment, chunk_size))
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

// 辅助方法实现
void MemoryPool::recordPointerSource(void* ptr, bool from_pool, size_t size)
{
    std::lock_guard<std::mutex> lock(pointer_mutex_);
    pointer_sources_[ptr] = std::make_pair(from_pool, size);
}

MemoryPool::MemoryBlock* MemoryPool::findMemoryBlock(LayeredPool* pool, void* ptr)
{
    // 在已分配的块中查找（简化实现：总是返回nullptr，创建新块）
    // 完整实现需要维护已分配块的列表或使用更复杂的数据结构
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

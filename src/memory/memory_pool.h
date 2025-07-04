#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

#include <QObject>
#include <cstddef>              // size_t
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <unordered_set>
#include <cassert>              // assert
#include <unordered_map>

/**
 * @brief 高性能分层内存池
 *
 * 设计特点：
 * 1. 分层管理：小块(64B-1KB)、中块(1KB-64KB)、大块(64KB+)
 * 2. 预分配策略：启动时预分配，减少运行时系统调用
 * 3. 线程安全：使用原子操作和细粒度锁
 * 4. 内存对齐：支持SSE/AVX对齐要求
 *              SIMD = Single Instruction, Multiple Data（单指令多数据）,SE需要16字节对齐
 *              AVX (Advanced Vector Extensions) 是 Intel 提供的一套指令集扩展，用于加速并行计算 ,AVX需要32字节对齐  
 * 5. 统计信息：提供详细的性能统计
 */

class MemoryPool
{

public:
    // 内存池参数
    struct Config{
        size_t small_block_size;                // 小块内存
        size_t medium_block_size;               // 中块内存
        size_t initial_pool_size;               // 内存池默认 16 MB
        size_t max_pool_size;                   // 内存池最大 512 MB
        size_t alignment;                       // 内存对齐字节数（AUX对齐）
        bool enable_statistics;                 // 启用统计模式
        bool enable_debug;                      // 启用调试模式

        Config()
            : small_block_size(1024)               // 1 KB
            , medium_block_size(65536)             // 64 KB
            , initial_pool_size(16 * 1024 * 1024)  // 16 MB
            , max_pool_size(512 * 1024 * 1024)     // 512 MB
            , alignment(32)                        // AVX对齐
            , enable_statistics(true)              // 是否启用统计
            , enable_debug(false)                  // 是否启用调试  
        {}
    };

    // 首先，创建一个用于返回的非原子版本的统计信息
    // 外部使用（普通数据）
    struct StatisticsSnapshot {
        size_t total_allocated;         // 总分配字节数
        size_t total_freed;             // 总释放字节数
        size_t current_usage;           // 当前使用量
        size_t peak_usage;              // 峰值使用量
        size_t allocation_count;        // 分配次数
        size_t free_count;              // 释放次数
        size_t pool_hit_count;          // 池命中次数
        size_t system_alloc_count;      // 系统分配次数

        // 计算命中率 —— 命中 / 总分配
        double getHitRate() const {
            return allocation_count > 0 ? static_cast<double>(pool_hit_count) / allocation_count : 0.0;
        }

        // 计算未使用率 —— 1.0 - 当前使用量 / 峰值使用量
        double getUnusedMemoryRatio() const {
            return peak_usage > 0 ? 1.0 - static_cast<double>(current_usage) / peak_usage : 0.0;
        }

        // 占位符：真正的碎片率需要访问池内部结构，在MemoryPool类中实现
        double getFragmentationRate() const {
            // 这个方法在StatisticsSnapshot中无法实现，因为需要访问池的内部状态
            // 实际实现在MemoryPool::getFragmentationRate()中
            return 0.0;
        }
    };

    // 内部使用（线程安全）
    struct Statistics{
        std::atomic<size_t> total_allocated{0};         // 总分配字节数
        std::atomic<size_t> total_freed{0};             // 总释放字节数
        std::atomic<size_t> current_usage{0};           // 当前使用量
        std::atomic<size_t> peak_usage{0};              // 峰值使用量
        std::atomic<size_t> allocation_count{0};        // 分配次数
        std::atomic<size_t> free_count{0};              // 释放次数
        std::atomic<size_t> pool_hit_count{0};          // 池命中次数
        std::atomic<size_t> system_alloc_count{0};      // 系统分配次数

        // 转换为快照
        StatisticsSnapshot getSnapshot() const {
            return StatisticsSnapshot{
                total_allocated.load(),
                total_freed.load(),
                current_usage.load(),
                peak_usage.load(),
                allocation_count.load(),
                free_count.load(),
                pool_hit_count.load(),
                system_alloc_count.load()
            };
        }
    };

    /**
     * // 测试代码
     * for(int i = 0; i < 1000000; ++i) {
     *  // 方式1：直接访问原子变量
     *  auto rate1 = static_cast<double>(stats_.pool_hit_count.load()) / 
     *                 stats_.allocation_count.load();
     *  // 耗时：~2000μs (每次两个原子操作)
     * 
     *  // 方式2：使用快照
     *  auto snapshot = getStatistics();
     *  auto rate2 = snapshot.getHitRate();
     *  // 耗时：~50μs (一次性快照 + 普通计算)
     * }
     */

private:
    // 内存块结构 - 构成内存块链表
    struct MemoryBlock{
        void* data;             // 数据指针
        size_t size;            // 块大小
        bool is_free;           // 是否空闲
        MemoryBlock* next;      // 下一个块
        MemoryBlock* prev;      // 上一个块

        MemoryBlock(void* ptr, size_t sz)
            :data(ptr)
            ,size(sz)
            ,is_free(true)
            ,next(nullptr)
            ,prev(nullptr)
        {}
    };

    // 分层池结构 - 管理内存块
    struct LayeredPool{
        std::vector<std::unique_ptr<uint8_t[]>> chunks;     // chunks 用智能指针管理申请的内存, 将智能指针放到 chunks 这个动态数组中
        MemoryBlock* free_list;                             // 可用链表
        std::mutex mutex;                                   // 线程锁
        size_t block_size;                                  // 块大小
        size_t blocks_per_chunk;                            // 每个大块内存chunk的块数

        LayeredPool(size_t bs, size_t bpc)
            :free_list(nullptr)
            ,block_size(bs)
            ,blocks_per_chunk(bpc)
        {}
    };

public:
    explicit MemoryPool(const Config& config = Config{});
    ~MemoryPool();

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

public:
    /**
     * @brief 分配内存,优先从内存池分配,如果内存池中没有足够的空间,则从系统分配
     * @param size 需要分配的字节数
     * @param alignment 对齐要求（0表示使用默认对齐）
     * @return 分配的内存指针，失败返回nullptr
     */
    void* allocate(size_t size, size_t alignment = 0);

    /**
     * @brief 释放内存
     * @param ptr 要释放的内存指针
     */
    void deallocate(void* ptr);

    /**
     * @brief 整理内存碎片
     * 合并相邻的空闲块(地址连续才合并)，提高内存利用率
     */
    void defragment();

public:
    /**
     * @brief 检查内存池状态
     * @return true表示状态正常
     */
    bool isHealthy() const;

    /**
     * @brief 获取统计信息
     * @return 统计信息的拷贝
     */
    StatisticsSnapshot getStatistics() const {
        return stats_.getSnapshot();
    }

    /**
     * @brief 重置统计信息
     */
    void resetStatistics();

    /**
     * @brief 获取内存池使用报告
     * @return 格式化的报告字符串
     */
    std::string getReport() const;

    /**
     * @brief 获取真正的内存碎片率
     * @return 0.0-1.0 之间的值，0表示无碎片，1表示严重碎片化
     */
    double getFragmentationRate() const;

    /**
     * @brief 获取内存利用率（相对于峰值）
     * @return 0.0-1.0 之间的值，1表示达到历史峰值
     */
    double getMemoryUtilizationRate() const;

    /**
     * @brief 获取详细的内存池健康报告
     * @return 包含碎片化、利用率等详细信息的结构
     */
    struct HealthReport {
        double fragmentation_rate;       // 真正的碎片率
        double utilization_rate;         // 内存利用率
        double unused_ratio;            // 未使用内存比例
        size_t total_free_blocks;       // 总空闲块数
        size_t largest_free_block;      // 最大连续空闲块
        size_t smallest_free_block;     // 最小空闲块
        size_t average_free_block_size; // 平均空闲块大小
        double free_block_size_variance; // 空闲块大小方差
    };

    /**
     * @brief 获取详细健康报告
     */
    HealthReport getHealthReport() const;

private:
    /**
     * @brief 根据大小选择合适的池
     */
    LayeredPool* selectPool(size_t size);

    /**
     * @brief 从指定池分配内存
     */
    void* allocateFromPool(LayeredPool* pool, size_t size);

    /**
     * @brief 从内存块的链表获得内存块
     */
    void *allocateBlock(LayeredPool *pool, MemoryBlock *block);

    /**
     * @brief 尝试拓展内存池并分配内存
     */
    void *tryExpandAndAllocate(LayeredPool *pool, size_t size);

    /**
     * @brief 真正的分配内存
     */
    bool allocateChunk(LayeredPool* pool);

    /**
     * @brief 对齐内存地址
     */
    void* alignPointer(void* ptr, size_t alignment);

    /**
     * @brief 计算对齐后的大小
     */
    size_t alignSize(size_t size, size_t alignment);

    /**
     * @brief 更新统计信息
     */
    void updateStatistics(size_t size, bool is_allocation, bool from_pool);

    /**
     * @brief 调试模式下的内存检查
     */
    void debugTrackAllocation(void* ptr, size_t size);
    void debugTrackDeallocation(void* ptr);

    /**
     * @brief 将内存归还到池中
     */
    void deallocateToPool(void* ptr);

    /**
     * @brief 记录指针来源和大小
     */
    void recordPointerSource(void* ptr, bool from_pool, size_t size);
    
    /**
     * @brief 查找内存块
     */
    MemoryBlock* findMemoryBlock(LayeredPool* pool, void* ptr);
    
    /**
     * @brief 格式化字节数显示
     */
    std::string formatBytes(size_t bytes) const;
    
    /**
     * @brief 获取池状态信息
     */
    std::string getPoolStatus() const;

    /**
     * @brief 计算单个池的碎片信息
     */
    struct PoolFragmentInfo {
        size_t total_free_memory;
        size_t largest_free_block;
        size_t free_block_count;
        std::vector<size_t> free_block_sizes;
    };

    PoolFragmentInfo analyzePoolFragmentation(LayeredPool* pool) const;

private:
    Config config_;             // 配置信息
    mutable Statistics stats_;  // 统计信息

    // 分层池
    std::unique_ptr<LayeredPool> small_pool_;
    std::unique_ptr<LayeredPool> medium_pool_;
    std::unique_ptr<LayeredPool> large_pool_;

    // 全局状态
    std::atomic<bool> is_shutdown_{false};

    // 调试信息（仅在调试模式下使用）
    mutable std::mutex debug_mutex_;
    std::unordered_set<void*> allocated_pointers_; // 检测内存泄露、只关心该指针是否已释放

    // 指针来源跟踪, pointer_sources_ 关心"这块内存从哪来，多大，该怎么还"
    mutable std::mutex pointer_mutex_;
    std::unordered_map<void*, std::pair<bool, size_t>> pointer_sources_;  // true=池分配, false=系统分配
};


/**
 * @brief 内存池分配器适配器
 * 用于STL容器的自定义分配器
 */

template<typename T>
class MemoryPoolAllocator{
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using different_type = std::ptrdiff_t;

    // 这是什么设计？
    template<typename U>
    struct rebind{
        using other = MemoryPoolAllocator<U>;
    };

    explicit MemoryPoolAllocator(MemoryPool* pool)
        :pool_(pool){ assert(pool_ != nullptr); }

    template<typename U>
    MemoryPoolAllocator(const MemoryPoolAllocator<U>& other)
        :pool_(other.pool){}

    pointer allocate(size_type n){
        return static_cast<pointer>(pool_->allocate( n * sizeof(T)));
    }

    void deallocate(pointer p, size_type n){
        pool_->deallocate(p);
    }

    template<typename U>
    bool operator==(const MemoryPoolAllocator<U>& other) const{
        return pool_ == other.pool_;
    }

    template<typename U>
    bool operator!=(const MemoryPoolAllocator<U>& other) const{
        return !(*this == other);
    }

private:
    template<typename U>
    friend class MemoryPoolAllocator;

    MemoryPool* pool_;
};

#endif // MEMORY_POOL_H

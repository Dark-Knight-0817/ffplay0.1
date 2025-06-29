#ifndef PACKET_RECYCLER_H
#define PACKET_RECYCLER_H

#include <memory>
#include <vector>
#include <queue>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>         // 添加这个头文件
#include <condition_variable>  // 可能也需要这个

// 前向声明
struct AVPacket;

/**
 * @brief 高效的AVPacket回收系统
 *
 * 设计特点：
 * 1. 大小分类：按packet大小分不同的池，避免内存浪费
 * 2. 引用计数：支持packet的共享使用和安全回收
 * 3. 批量回收：减少锁竞争，提升多线程性能
 * 4. 内存压缩：定期整理碎片，优化内存使用
 * 5. 统计分析：详细的大小分布和使用模式分析
 * 6. 自适应调整：根据使用模式动态调整池大小
 */
class PacketRecycler {
public:
    /**
     * @brief 数据包大小类别
     */
    enum class SizeCategory {
        TINY = 0,      // < 1KB      (音频帧、控制信息)
        SMALL,         // 1KB - 16KB  (小视频帧、音频块)
        MEDIUM,        // 16KB - 256KB (标准视频帧)
        LARGE,         // 256KB - 1MB  (I帧、高质量帧)
        EXTRA_LARGE,          // > 1MB        (超高质量、4K帧)
        CATEGORY_COUNT
    };

    /**
     * @brief 回收器配置
     */
    struct Config {
        size_t max_pools_per_category;     // 每个类别的最大池数
        size_t packets_per_pool;           // 每个池的packet数量
        size_t max_total_memory;           // 最大总内存使用量
        bool enable_batch_recycling;      // 启用批量回收
        bool enable_reference_counting;   // 启用引用计数
        bool enable_statistics;           // 启用统计功能
        size_t cleanup_interval_ms;       // 清理间隔(毫秒)
        double memory_pressure_threshold; // 内存压力阈值(0.0-1.0)

        Config()
            : max_pools_per_category(8)
            , packets_per_pool(32)
            , max_total_memory(128 * 1024 * 1024)  // 128MB
            , enable_batch_recycling(true)
            , enable_reference_counting(true)
            , enable_statistics(true)
            , cleanup_interval_ms(30000)  // 30秒
            , memory_pressure_threshold(0.8)
        {}
    };

    /**
     * @brief 统计信息快照
     */
    struct StatisticsSnapshot {
        size_t total_created;      // 总创建数量
        size_t total_acquired;     // 总获取次数
        size_t total_released;     // 总归还次数
        size_t current_in_use;     // 当前使用中
        size_t current_available;  // 当前可用数量
        size_t peak_usage;         // 峰值使用量

        // 计算命中率
        double getHitRate() const {
            return total_acquired > 0 ? static_cast<double>(total_acquired - total_created) / total_acquired : 0.0;
        }
    };

    /**
     * @brief 统计信息
     */
    struct Statistics {
        std::atomic<size_t> total_created{0};      // 总创建数量
        std::atomic<size_t> total_acquired{0};     // 总获取次数
        std::atomic<size_t> total_released{0};     // 总归还次数
        std::atomic<size_t> current_in_use{0};     // 当前使用中
        std::atomic<size_t> current_available{0};  // 当前可用数量
        std::atomic<size_t> peak_usage{0};         // 峰值使用量

        // 转换为快照
        StatisticsSnapshot getSnapshot() const {
            return StatisticsSnapshot{
                total_created.load(),
                total_acquired.load(),
                total_released.load(),
                current_in_use.load(),
                current_available.load(),
                peak_usage.load()
            };
        }
    };

    /**
     * @brief 引用计数包装器
     */
    class RefCountedPacket {
    public:
        explicit RefCountedPacket(AVPacket* packet, PacketRecycler* recycler);
        ~RefCountedPacket();

        // 禁用拷贝，支持移动
        RefCountedPacket(const RefCountedPacket&) = delete;
        RefCountedPacket& operator=(const RefCountedPacket&) = delete;
        RefCountedPacket(RefCountedPacket&& other) noexcept;
        RefCountedPacket& operator=(RefCountedPacket&& other) noexcept;

        AVPacket* get() const { return packet_; }
        AVPacket* operator->() const { return packet_; }
        AVPacket& operator*() const { return *packet_; }

        // 创建共享引用
        std::shared_ptr<RefCountedPacket> share();

        // 获取引用计数
        int getRefCount() const { return ref_count_.load(); }

        explicit operator bool() const { return packet_ != nullptr; }

    private:
        AVPacket* packet_;
        PacketRecycler* recycler_;
        std::atomic<int> ref_count_;

        void addRef();
        void release();

        friend class PacketRecycler;
    };

    using PacketPtr = std::unique_ptr<RefCountedPacket>;

private:
    /**
     * @brief 单个数据包池
     */
    class PacketPool {
    public:
        PacketPool(SizeCategory category, size_t target_size, size_t capacity);
        ~PacketPool();

        AVPacket* acquire();
        bool release(AVPacket* packet);

        size_t available() const;
        size_t capacity() const { return capacity_; }
        size_t getTargetSize() const { return target_size_; }
        SizeCategory getCategory() const { return category_; }

        // 批量操作
        std::vector<AVPacket*> acquireBatch(size_t count);
        size_t releaseBatch(const std::vector<AVPacket*>& packets);

        // 统计信息
        size_t getTotalAllocated() const { return total_allocated_; }
        size_t getMemoryUsage() const;

        // 清理空闲packet
        void cleanup(size_t keep_count = 0);

    private:
        SizeCategory category_;
        size_t target_size_;        // 目标packet大小
        size_t capacity_;           // 池容量

        std::vector<AVPacket*> available_packets_;
        mutable std::mutex mutex_;
        std::atomic<size_t> total_allocated_{0};

        AVPacket* createPacket();
        void destroyPacket(AVPacket* packet);
        bool allocateBuffer(AVPacket* packet, size_t size);
    };

public:
    /**
     * @brief 构造函数
     * @param config 回收器配置
     */
    explicit PacketRecycler(const Config& config = Config{});

    /**
     * @brief 析构函数
     */
    ~PacketRecycler();

    // 禁用拷贝和赋值
    PacketRecycler(const PacketRecycler&) = delete;
    PacketRecycler& operator=(const PacketRecycler&) = delete;

    /**
     * @brief 分配数据包
     * @param size 所需的缓冲区大小
     * @return 分配的数据包智能指针
     */
    PacketPtr allocatePacket(size_t size);

    /**
     * @brief 批量分配数据包
     * @param sizes 各个packet的大小要求
     * @return 分配的数据包列表
     */
    std::vector<PacketPtr> allocatePacketBatch(const std::vector<size_t>& sizes);

    /**
     * @brief 获取统计信息
     */
    StatisticsSnapshot getStatistics() const { return stats_.getSnapshot(); }

    /**
     * @brief 获取各类别的详细信息
     */
    std::vector<std::tuple<SizeCategory, size_t, size_t, size_t>> getCategoryInfo() const;

    /**
     * @brief 强制垃圾回收
     * 清理所有空闲的数据包以释放内存
     */
    void forceGarbageCollection();

    /**
     * @brief 设置内存压力回调
     */
    void setMemoryPressureCallback(std::function<void(size_t current, size_t max)> callback);

    /**
     * @brief 预热指定类别的池
     * @param category 数据包类别
     * @param count 预分配数量
     */
    void warmupCategory(SizeCategory category, size_t count);

    /**
     * @brief 动态调整池配置
     * 根据使用模式自动调整各池的大小
     */
    void optimizePools();

    /**
     * @brief 获取内存使用报告
     */
    std::string getMemoryReport() const;

    /**
     * @brief 手动回收数据包（内部使用）
     */
    void recyclePacket(AVPacket* packet, SizeCategory category);

private:
    /**
     * @brief 根据大小确定类别
     */
    SizeCategory categorizeSize(size_t size) const;

    /**
     * @brief 获取类别的建议大小
     */
    size_t getCategorySuggestedSize(SizeCategory category) const;

    /**
     * @brief 获取或创建指定类别和大小的池
     */
    std::shared_ptr<PacketPool> getOrCreatePool(SizeCategory category, size_t target_size);

    /**
     * @brief 检查内存压力并触发清理
     */
    void checkMemoryPressure();

    /**
     * @brief 更新统计信息
     */
    void updateStatistics(SizeCategory category, size_t size, bool is_allocation, bool from_pool);

    /**
     * @brief 后台清理线程
     */
    void cleanupThread();

    /**
     * @brief 启动清理线程
     */
    void startCleanupThread();

    /**
     * @brief 停止清理线程
     */
    void stopCleanupThread();

private:
    Config config_;                                          // 配置信息
    mutable Statistics stats_;                               // 统计信息

    // 多级池映射：类别 -> 大小 -> 池
    mutable std::mutex pools_mutex_;
    std::unordered_map<SizeCategory,
                       std::unordered_map<size_t, std::shared_ptr<PacketPool>>> pools_;

    std::function<void(size_t, size_t)> memory_pressure_callback_;  // 内存压力回调

    // 清理线程相关
    std::thread cleanup_thread_;
    std::atomic<bool> cleanup_running_{false};
    std::atomic<bool> shutdown_{false};
    std::condition_variable cleanup_cv_;
    std::mutex cleanup_mutex_;
};

/**
 * @brief 全局数据包回收器
 */
class GlobalPacketRecycler {
public:
    static PacketRecycler& getInstance() {
        static PacketRecycler instance;
        return instance;
    }

    static void initialize(const PacketRecycler::Config& config) {
        // 由于单例模式的限制，这里只能记录配置建议
        // 但无法重新配置已存在的实例
    }

private:
    GlobalPacketRecycler() = default;
};

/**
 * @brief 便捷的数据包分配宏
 */
#define ALLOCATE_PACKET(size) \
GlobalPacketRecycler::getInstance().allocatePacket(size)

#define ALLOCATE_PACKET_BATCH(sizes) \
    GlobalPacketRecycler::getInstance().allocatePacketBatch(sizes)

    /**
 * @brief 数据包大小常量
 */
    namespace PacketSizes {
    constexpr size_t TINY_MAX = 1024;           // 1KB
    constexpr size_t SMALL_MAX = 16 * 1024;     // 16KB
    constexpr size_t MEDIUM_MAX = 256 * 1024;   // 256KB
    constexpr size_t LARGE_MAX = 1024 * 1024;   // 1MB

    constexpr size_t AUDIO_TYPICAL = 4 * 1024;      // 4KB 典型音频帧
    constexpr size_t VIDEO_SD_TYPICAL = 64 * 1024;  // 64KB SD视频帧
    constexpr size_t VIDEO_HD_TYPICAL = 256 * 1024; // 256KB HD视频帧
    constexpr size_t VIDEO_4K_TYPICAL = 1024 * 1024; // 1MB 4K视频帧
}

#endif // PACKET_RECYCLER_H

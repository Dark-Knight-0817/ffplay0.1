#ifndef MEMORY_MANAGER_H
#define MEMORY_MANAGER_H

#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <functional>
#include <thread>
#include <chrono>

// 包含所有内存管理组件
#include "memory_pool.h"
#include "memory_tracker.h"
#include "object_pool.h"
#include "frame_allocator.h"
#include "packet_recycler.h"
#include "cache_manager.h"
#include "smart_pointers.h"

/**
 * @brief 统一内存管理系统
 *
 * 设计特点：
 * 1. 配置管理：全局内存管理配置和策略选择
 * 2. 策略选择：根据场景选择最优的内存管理策略
 * 3. 资源协调：各个组件间的资源协调和优化
 * 4. 性能调优：运行时动态调优和监控
 * 5. 监控集成：统一的监控接口和报告生成
 * 6. 异常处理：内存不足等异常情况的处理
 */
class MemoryManager {
public:
    /**
     * @brief 内存管理策略
     */
    enum class Strategy {
        PERFORMANCE,    // 性能优先：大内存池，多对象池
        MEMORY_SAVING,  // 内存节约：小池，及时回收
        BALANCED,       // 平衡模式：性能和内存的平衡
        CUSTOM         // 自定义策略
    };

    /**
     * @brief 应用场景类型
     */
    enum class ScenarioType {
        SINGLE_STREAM,     // 单路流处理
        MULTI_STREAM,      // 多路流处理
        REAL_TIME,         // 实时处理
        BATCH_PROCESSING,  // 批处理
        LOW_LATENCY,       // 低延迟场景
        HIGH_THROUGHPUT    // 高吞吐场景
    };

    /**
     * @brief 统一配置
     */
    struct Config {
        Strategy strategy;                        // 管理策略
        ScenarioType scenario;                    // 应用场景
        size_t max_total_memory;                 // 最大总内存限制
        bool enable_global_tracking;            // 启用全局跟踪
        bool enable_auto_optimization;          // 启用自动优化
        bool enable_memory_pressure_handling;   // 启用内存压力处理
        size_t optimization_interval_ms;        // 优化间隔
        double memory_pressure_threshold;       // 内存压力阈值

        // 各组件开关
        bool use_memory_pool;
        bool use_object_pools;
        bool use_frame_allocator;
        bool use_packet_recycler;
        bool use_cache_manager;

        Config()
            : strategy(Strategy::BALANCED)
            , scenario(ScenarioType::MULTI_STREAM)
            , max_total_memory(1024 * 1024 * 1024)  // 1GB
            , enable_global_tracking(true)
            , enable_auto_optimization(true)
            , enable_memory_pressure_handling(true)
            , optimization_interval_ms(60000)  // 1分钟
            , memory_pressure_threshold(0.85)
            , use_memory_pool(true)
            , use_object_pools(true)
            , use_frame_allocator(true)
            , use_packet_recycler(true)
            , use_cache_manager(true)
        {}
    };

    /**
     * @brief 全局内存统计
     */
    struct GlobalStatistics {
        MemoryPool::StatisticsSnapshot pool_stats;      // 改为使用 Snapshot 版本
        MemoryTracker::StatisticsSnapshot tracker_stats; // 改为使用 Snapshot 版本
        FrameAllocator::StatisticsSnapshot frame_stats;  // 改为使用 Snapshot 版本
        PacketRecycler::StatisticsSnapshot packet_stats; // 改为使用 Snapshot 版本

        size_t total_memory_usage;
        size_t total_objects;
        double overall_efficiency;

        GlobalStatistics()
            : total_memory_usage(0)
            , total_objects(0)
            , overall_efficiency(0.0) {}
    };

    /**
     * @brief 内存压力级别
     */
    enum class PressureLevel {
        LOW,       // 内存充足
        MODERATE,  // 中等压力
        HIGH,      // 高压力
        CRITICAL   // 临界状态
    };

    /**
     * @brief 内存压力事件
     */
    struct PressureEvent {
        PressureLevel level;
        size_t current_usage;
        size_t max_usage;
        std::chrono::steady_clock::time_point timestamp;
        std::string description;

        PressureEvent(PressureLevel lvl, size_t current, size_t max_mem, const std::string& desc)
            : level(lvl), current_usage(current), max_usage(max_mem)
            , timestamp(std::chrono::steady_clock::now()), description(desc) {}
    };

public:
    /**
     * @brief 构造函数
     * @param config 内存管理配置
     */
    explicit MemoryManager(const Config& config = Config{});

    /**
     * @brief 析构函数
     */
    ~MemoryManager();

    // 禁用拷贝和赋值
    MemoryManager(const MemoryManager&) = delete;
    MemoryManager& operator=(const MemoryManager&) = delete;

    /**
     * @brief 初始化内存管理系统
     * @return 是否成功初始化
     */
    bool initialize();

    /**
     * @brief 关闭内存管理系统
     */
    void shutdown();

    /**
     * @brief 获取组件实例
     */
    MemoryPool& getMemoryPool();
    MemoryTracker& getMemoryTracker();
    FrameAllocator& getFrameAllocator();
    PacketRecycler& getPacketRecycler();

    template<typename Key, typename Value>
    CacheManager<Key, Value>& getCacheManager();

    /**
     * @brief 统一内存分配接口
     * @param size 分配大小
     * @param alignment 对齐要求
     * @param hint 分配提示
     * @return 分配的内存指针
     */
    void* allocate(size_t size, size_t alignment = 0, const std::string& hint = "");

    /**
     * @brief 统一内存释放接口
     * @param ptr 要释放的指针
     */
    void deallocate(void* ptr);

    /**
     * @brief 获取全局统计信息
     */
    GlobalStatistics getGlobalStatistics() const;

    /**
     * @brief 获取当前内存压力级别
     */
    PressureLevel getCurrentPressureLevel() const;

    /**
     * @brief 强制执行垃圾回收
     */
    void forceGarbageCollection();

    /**
     * @brief 优化内存配置
     * 根据当前使用模式动态调整各组件配置
     */
    void optimizeConfiguration();

    /**
     * @brief 生成综合报告
     */
    std::string generateComprehensiveReport() const;

    /**
     * @brief 设置内存压力回调
     */
    void setMemoryPressureCallback(std::function<void(const PressureEvent&)> callback);

    /**
     * @brief 设置性能监控回调
     */
    void setPerformanceCallback(std::function<void(const GlobalStatistics&)> callback);

    /**
     * @brief 应用预设配置
     * @param scenario 应用场景
     */
    void applyScenarioConfig(ScenarioType scenario);

    /**
     * @brief 启用/禁用组件
     */
    void enableComponent(const std::string& component_name, bool enable);

    /**
     * @brief 设置内存限制
     */
    void setMemoryLimit(size_t max_bytes);

    /**
     * @brief 获取内存使用趋势
     * @param duration_minutes 统计时长（分钟）
     */
    std::vector<std::pair<std::chrono::steady_clock::time_point, size_t>>
    getMemoryUsageTrend(int duration_minutes = 60) const;

private:
    /**
     * @brief 初始化各个组件
     */
    bool initializeComponents();

    /**
     * @brief 应用策略配置
     */
    void applyStrategy(Strategy strategy);

    /**
     * @brief 监控线程
     */
    void monitoringThread();

    /**
     * @brief 优化线程
     */
    void optimizationThread();

    /**
     * @brief 检查内存压力
     */
    void checkMemoryPressure();

    /**
     * @brief 处理内存压力
     */
    void handleMemoryPressure(PressureLevel level);

    /**
     * @brief 收集全局统计
     */
    void collectGlobalStatistics();

    /**
     * @brief 记录内存使用历史
     */
    void recordMemoryUsage();

    /**
     * @brief 启动后台线程
     */
    void startBackgroundThreads();

    /**
     * @brief 停止后台线程
     */
    void stopBackgroundThreads();

private:
    Config config_;                                           // 配置信息
    mutable std::mutex config_mutex_;                         // 配置锁

    // 组件实例
    std::unique_ptr<MemoryPool> memory_pool_;
    std::unique_ptr<MemoryTracker> memory_tracker_;
    std::unique_ptr<FrameAllocator> frame_allocator_;
    std::unique_ptr<PacketRecycler> packet_recycler_;

    // 缓存管理器映射（支持不同类型）
    mutable std::mutex cache_managers_mutex_;
    std::unordered_map<std::string, std::shared_ptr<void>> cache_managers_;

    // 统计和监控
    mutable std::mutex stats_mutex_;
    GlobalStatistics global_stats_;
    std::vector<std::pair<std::chrono::steady_clock::time_point, size_t>> memory_history_;

    // 回调函数
    std::function<void(const PressureEvent&)> pressure_callback_;
    std::function<void(const GlobalStatistics&)> performance_callback_;

    // 后台线程
    std::thread monitoring_thread_;
    std::thread optimization_thread_;
    std::atomic<bool> monitoring_running_{false};
    std::atomic<bool> optimization_running_{false};
    std::atomic<bool> shutdown_{false};
    std::condition_variable monitoring_cv_;
    std::condition_variable optimization_cv_;
    std::mutex monitoring_mutex_;
    std::mutex optimization_mutex_;

    // 性能计时
    mutable std::mutex timing_mutex_;
    std::chrono::steady_clock::time_point last_allocation_time_;
    std::chrono::steady_clock::time_point last_deallocation_time_;
    double allocation_time_accumulator_{0.0};
    double deallocation_time_accumulator_{0.0};
    size_t allocation_count_{0};
    size_t deallocation_count_{0};

    // 状态标志
    std::atomic<bool> initialized_{false};
    std::atomic<PressureLevel> current_pressure_level_{PressureLevel::LOW};
};

/**
 * @brief 全局内存管理器单例
 */
class GlobalMemoryManager {
public:
    static MemoryManager& getInstance() {
        static MemoryManager instance;
        return instance;
    }

    static bool initialize(const MemoryManager::Config& config = MemoryManager::Config{}) {
        getInstance().~MemoryManager();
        new (&getInstance()) MemoryManager(config);
        return getInstance().initialize();
    }

    static void shutdown() {
        getInstance().shutdown();
    }

private:
    GlobalMemoryManager() = default;
};

/**
 * @brief 便捷的全局访问宏
 */
#define GLOBAL_MEMORY_MANAGER GlobalMemoryManager::getInstance()
#define ALLOCATE_MEMORY(size) GLOBAL_MEMORY_MANAGER.allocate(size)
#define DEALLOCATE_MEMORY(ptr) GLOBAL_MEMORY_MANAGER.deallocate(ptr)
#define FORCE_GC() GLOBAL_MEMORY_MANAGER.forceGarbageCollection()

/**
 * @brief 内存管理RAII辅助类
 */
class MemoryScope {
public:
    explicit MemoryScope(const std::string& scope_name)
        : scope_name_(scope_name), start_time_(std::chrono::steady_clock::now()) {
        // 记录作用域开始时的内存状态
        start_stats_ = GLOBAL_MEMORY_MANAGER.getGlobalStatistics();
    }

    ~MemoryScope() {
        // 记录作用域结束时的内存状态和耗时
        auto end_stats = GLOBAL_MEMORY_MANAGER.getGlobalStatistics();
        auto duration = std::chrono::steady_clock::now() - start_time_;

        // 可以在这里记录日志或触发回调
        // logScopeStatistics(scope_name_, start_stats_, end_stats, duration);
    }

private:
    std::string scope_name_;
    std::chrono::steady_clock::time_point start_time_;
    MemoryManager::GlobalStatistics start_stats_;
};

/**
 * @brief 内存作用域宏
 */
#define MEMORY_SCOPE(name) MemoryScope _scope(name)

#endif // MEMORY_MANAGER_H

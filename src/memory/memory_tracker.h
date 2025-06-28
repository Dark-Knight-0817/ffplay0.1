#ifndef MEMORY_TRACKER_H
#define MEMORY_TRACKER_H

#include <QObject>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <string>
#include <memory>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <functional>
#include <vector>


/**
 * @brief 高级内存使用监控和分析系统
 *
 * 设计特点：
 * 1. 实时统计：当前使用量、峰值、分配次数等
 * 2. 泄漏检测：跟踪未释放的内存并提供调用栈
 * 3. 性能分析：分配热点、时间分布、大小分布
 * 4. 报告生成：详细的内存使用报告和图表数据
 * 5. 预警机制：内存使用过高时的回调通知
 * 6. 历史记录：保存一段时间的内存使用历史
 */
class MemoryTracker
{

public:
    /**
     * @brief 内存分配信息
     */
    struct AllocationInfo{
        void* ptr;                                          // 内存指针
        size_t size;                                        // 分配大小
        std::chrono::steady_clock::time_point timestamp;    // 分配时间
        std::string location;                               // 分配位置（文件：行号）
        std::thread::id thread_id;                          // 分配线程 ID

        AllocationInfo(void* p, size_t s, const std::string& loc)
            :ptr(p)
            ,size(s)
            ,timestamp(std::chrono::steady_clock::now())
            ,location(loc)
            ,thread_id(std::this_thread::get_id())
        {}
    };

    struct StatisticsSnapshot {
        size_t total_allocated;             // 累计分配字节数
        size_t total_freed;                 // 累计释放字节数
        size_t current_usage;               // 当前使用量
        size_t peak_usage;                  // 峰值使用量
        size_t allocation_count;            // 分配次数
        size_t free_count;                  // 释放次数
        size_t leak_count;                  // 泄漏次数

        // 计算平均分配大小
        double getAverageAllocationSize() const {
            return allocation_count > 0 ? static_cast<double>(total_allocated) / allocation_count : 0.0;
        }

        // 计算内存效率（释放率）
        double getMemoryEfficiency() const {
            return total_allocated > 0 ? static_cast<double>(total_freed) / total_allocated : 0.0;
        }
    };

    /**
 * @brief 内存使用统计（内部使用，带原子操作）
 */
    struct Statistics {
        std::atomic<size_t> total_allocated{0};             // 累计分配字节数
        std::atomic<size_t> total_freed{0};                 // 累计释放字节数
        std::atomic<size_t> current_usage{0};               // 当前使用量
        std::atomic<size_t> peak_usage{0};                  // 峰值使用量
        std::atomic<size_t> allocation_count{0};            // 分配次数
        std::atomic<size_t> free_count{0};                  // 释放次数
        std::atomic<size_t> leak_count{0};                  // 泄漏次数

        // 转换为快照
        StatisticsSnapshot getSnapshot() const {
            return StatisticsSnapshot{
                total_allocated.load(),
                total_freed.load(),
                current_usage.load(),
                peak_usage.load(),
                allocation_count.load(),
                free_count.load(),
                leak_count.load()
            };
        }
    };

    /**
     * @brief 内存使用快照
     */
    struct Snapshot{
        std::chrono::steady_clock::time_point timestamp;
        size_t current_usage;
        size_t allocation_count;
        size_t free_count;

        Snapshot()
            :timestamp(std::chrono::steady_clock::now())
            ,current_usage(0)
            ,allocation_count(0)
            ,free_count(0)
        {}
    };

    /**
     * @brief 预警回调函数类型
     */
    using AlertCallback = std::function<void(const std::string& message, size_t current_usage, size_t threshold)>;

    /**
     * @brief 配置选项
     */
    struct Config{
        bool enable_leak_detection;         // 启用泄漏检测
        bool enable_call_stack;             // 启用调用栈记录（性能影响较大）
        bool enable_statistics;              // 启用统计功能
        bool enable_history;                 // 启用历史记录
        size_t max_allocations;            // 最大跟踪的分配数量
        size_t alert_threshold; // 预警阈值（100MB)
        std::chrono::seconds history_interval;   // 历史记录间隔
        size_t max_history_size;             // 最大历史记录数量

        Config()
            :enable_leak_detection(true)
            ,enable_call_stack(false)
            ,enable_statistics(true)
            ,enable_history(true)
            ,max_allocations(100000)
            ,alert_threshold(100 * 1024 * 1024)
            ,history_interval(5)
            ,max_history_size(1000)
        {}
    };

public:
    explicit MemoryTracker(const Config& config = Config{});
    ~MemoryTracker();

    MemoryTracker(const MemoryTracker&) = delete;
    MemoryTracker& operator=(const MemoryTracker&) = delete;

public:
    /**
     * @brief 记录内存分配
     * @param ptr 分配的内存指针
     * @param size 分配的字节数
     * @param location 分配位置（通常是文件名:行号）
     */
    void recordAllocation(void* ptr,size_t size, const std::string& location = "");

    /**
     * @brief 记录内存释放
     * @param ptr 释放的内存指针
     * @return true表示成功找到并移除分配记录
     */
    bool recordDeallocation(void* ptr);

    /**
     * @brief 获取当前统计信息
     * @return 统计信息的拷贝
     */
    StatisticsSnapshot getStatistics() const { return stats_.getSnapshot(); }

    /**
     * @brief 检测内存泄漏
     * @return 泄漏的分配信息列表
     */
    std::vector<AllocationInfo> detectLeaks() const;

    /**
     * @brief 获取大小分布统计
     * @return 各个大小范围的分配次数
     */
    std::unordered_map<std::string, size_t> getSizeDistribution() const;

    /**
     * @brief 获取热点分析
     * @param top_n 返回前N个热点
     * @return 分配次数最多的位置
     */
    std::vector<Snapshot> getHistory() const;

    /**
     * @brief 获取热点分析
     * @param top_n 返回前N个热点
     * @return 分配次数最多的位置
     */
    std::vector<std::pair<std::string, size_t>> getHotspots(size_t top_n = 10) const;

    std::string generateReport() const;

    /**
    /**
     * @brief 生成性能报告的CSV数据
     * @return CSV格式的历史数据，可用于图表生成
     */
    std::string generateCSVData() const;

    /**
     * @brief 设置预警回调
     * @param callback 当内存使用超过阈值时的回调函数
     */
    void setAlertCallback(AlertCallback callback);

    /**
     * @brief 重置所有统计信息
     */
    void reset();

    /**
     * @brief 开始/停止历史记录
     */
    void startHistoryRecording();
    void stopHistoryRecording();

    /**
     * @brief 手动触发快照
     */
    void takeSnapshot();

    /**
     * @brief 检查是否健康（无泄漏、使用量正常）
     */
    bool isHealthy() const;

private:
    /**
     * @brief 历史记录线程函数
     */
    void historyRecordingThread();

    /**
     * @brief 检查并触发预警
     */
    void checkAndAlert(size_t current_usage);

    /**
     * @brief 清理过期的历史记录
     */
    void cleanupHistory();

    /**
     * @brief 分析分配大小并归类
     */
    std::string categorizeSize(size_t size) const;

private:
    Config config_;                         // 配置选项
    mutable Statistics stats_;              // 统计信息

    // 分配跟踪（仅在启用泄露检测时使用）
    mutable std::mutex allocations_mutex_;
    std::unordered_map<void*, AllocationInfo> active_allocations_;

    // 热点统计
    mutable std::mutex hotspots_mutex_;
    std::unordered_map<std::string, size_t> allocation_hotspots_;

    // 历史记录
    mutable std::mutex history_mutex_;
    std::vector<Snapshot> history_;
    std::thread history_thread_;
    std::atomic<bool> recording_history_{false};
    std::condition_variable history_cv_;

    // 预警系统
    AlertCallback alert_callback_;
    std::atomic<bool> alert_triggered_{false};

    // 生命周期管理
    std::atomic<bool> shutdown_{false};
};

#define MEMORY_TRACK_ALLOC(tracker, ptr, size) \
    do{ \
        if(tracker){ \
            tracker->recordAllocation(ptr, size, __FILE__":" std::to_string(__LINE__)); \
        }\
    }while(0)

#define MEMORY_TRACK_FREE(tracker, ptr) \
    do{ \
        if(tracker){ \
            tracker->recordDeallocation(ptr); \
        } \
    }while(0)

/**
 * @brief 全局内存跟踪器实例
 * 提供单例访问模式
 */
class GlobalMemoryTracker{
public:
    static MemoryTracker& getInstance(){
        static MemoryTracker instance;
        return instance;
    }

    static void configureGlobal(const MemoryTracker::Config& config) {
        static std::unique_ptr<MemoryTracker> instance_ptr;
        instance_ptr = std::make_unique<MemoryTracker>(config);
    }

private:
    GlobalMemoryTracker() = default;
};

#endif // MEMORY_TRACKER_H

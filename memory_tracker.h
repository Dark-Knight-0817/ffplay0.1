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
class memory_tracker
{
    Q_OBJECT
public:
    memory_tracker();

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
    }

    /**
     * @brief 内存使用统计
     */
    struct Statistics{
        std::atomic<size_t> total_allocated{0};             // 累计分配字节数
        std::atomic<size_t> total_freed{0};                 // 累计释放字节数
        std::atomic<size_t> current_usage{0};               // 当前使用量
        std::atomic<size_t> peak_usage{0};                  // 峰值使用量
        std::atomic<size_t> allocation_count{0};            // 分配次数
        std::atomic<size_t> free_count{0};                  // 释放次数
        std::atomic<size_t> leak_count{0};                  // 泄漏次数

        // 计算平均分配大小
        double getAverageAllocationSize() const{
            size_t count = allocation_count.load();
            return count > 0 ? static_cast<double> (total_allocated.load()) / count : 0.0;
        }

        // 计算内存效率（释放率）
        double getMemoryEfficiency() const{
            size_t allocated = total_allocated.load();
            return allocated > 0 ? static_cast<double>(total_freed.load()) / allocated : 0.0;
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
        bool enable_leadk_detection = true;         // 启用泄漏检测
        bool enable_call_stack = false;             // 启用调用栈记录（性能影响较大）
        bool enable_statistics = true;              // 启用统计功能
        bool enable_history = true;                 // 启用历史记录
        size_t max_allocations = 100000;            // 最大跟踪的分配数量
        size_t alert_threshold = 100 * 1024 * 1024; // 预警阈值（100MB)
        std::chrono::seconds history_interval{5};   // 历史记录间隔
        size_t max_history_size = 1000;             // 最大历史记录数量
    };
};

#endif // MEMORY_TRACKER_H

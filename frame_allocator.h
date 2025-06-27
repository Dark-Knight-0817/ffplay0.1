#ifndef FRAME_ALLOCATOR_H
#define FRAME_ALLOCATOR_H

#include <memory>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <atomic>
#include <string>

// 前向声明，避免循环依赖
struct AVFrame;
enum AVPixelFormat : int;

/**
 * @brief 视频帧专用内存分配器
 *
 * 设计特点：
 * 1. 按分辨率分类：为常见分辨率维护专用池
 * 2. 格式支持：支持YUV420P、RGB24、NV12等主流格式
 * 3. 内存对齐：AVX512对齐优化，提升处理性能
 * 4. 零拷贝优化：支持GPU内存映射
 * 5. 预分配策略：根据视频参数智能预分配
 * 6. 内存压缩：自动检测和清理未使用的帧缓冲
 */
class FrameAllocator {
public:
    /**
     * @brief 帧规格定义
     */
    struct FrameSpec {
        int width;
        int height;
        int pixel_format;  // AVPixelFormat
        int alignment;     // 内存对齐要求

        FrameSpec(int w = 0, int h = 0, int fmt = 0, int align = 32)
            : width(w), height(h), pixel_format(fmt), alignment(align) {}

        // 用于unordered_map的key
        bool operator==(const FrameSpec& other) const {
            return width == other.width &&
                   height == other.height &&
                   pixel_format == other.pixel_format &&
                   alignment == other.alignment;
        }
    };

    /**
     * @brief 分配器配置
     */
    struct Config {
        size_t max_pools;                  // 最大池数量
        size_t frames_per_pool;           // 每个池的帧数量
        size_t max_frame_size;            // 最大单帧大小(bytes)
        bool enable_gpu_memory;           // 是否启用GPU内存映射
        bool enable_statistics;           // 是否启用统计
        int default_alignment;            // 默认对齐字节数

        Config()
            : max_pools(32)
            , frames_per_pool(16)
            , max_frame_size(64 * 1024 * 1024)  // 64MB
            , enable_gpu_memory(false)
            , enable_statistics(true)
            , default_alignment(32)
        {}
    };

    /**
     * @brief 分配统计信息
     */
    struct Statistics {
        std::atomic<size_t> total_allocated{0};        // 总分配次数
        std::atomic<size_t> total_freed{0};            // 总释放次数
        std::atomic<size_t> pool_hits{0};              // 池命中次数
        std::atomic<size_t> pool_misses{0};            // 池未命中次数
        std::atomic<size_t> active_pools{0};           // 活跃池数量
        std::atomic<size_t> total_memory_usage{0};     // 总内存使用量
        std::atomic<size_t> peak_memory_usage{0};      // 峰值内存使用量

        // 计算池命中率
        double getHitRate() const {
            size_t total = pool_hits.load() + pool_misses.load();
            return total > 0 ? static_cast<double>(pool_hits.load()) / total : 0.0;
        }

        // 计算内存效率
        double getMemoryEfficiency() const {
            size_t peak = peak_memory_usage.load();
            size_t current = total_memory_usage.load();
            return peak > 0 ? static_cast<double>(current) / peak : 0.0;
        }
    };

    /**
     * @brief 帧分配结果
     */
    struct AllocatedFrame {
        AVFrame* frame;           // 分配的帧指针
        size_t buffer_size;       // 缓冲区大小
        bool from_pool;          // 是否来自池
        FrameSpec spec;          // 帧规格

        AllocatedFrame() : frame(nullptr), buffer_size(0), from_pool(false) {}
    };

private:
    /**
     * @brief 单个帧池
     */
    class FramePool {
    public:
        explicit FramePool(const FrameSpec& spec, size_t capacity);
        ~FramePool();

        AVFrame* acquire();
        bool release(AVFrame* frame);

        size_t available() const { return available_frames_.size(); }
        size_t capacity() const { return capacity_; }
        const FrameSpec& getSpec() const { return spec_; }

        // 统计信息
        size_t getTotalAllocated() const { return total_allocated_; }
        size_t getMemoryUsage() const;

    private:
        FrameSpec spec_;
        size_t capacity_;
        std::vector<AVFrame*> available_frames_;
        mutable std::mutex mutex_;
        std::atomic<size_t> total_allocated_{0};

        AVFrame* createFrame();
        void destroyFrame(AVFrame* frame);
        bool allocateBuffer(AVFrame* frame);
    };

public:
    /**
     * @brief 构造函数
     * @param config 分配器配置
     */
    explicit FrameAllocator(const Config& config = Config{});

    /**
     * @brief 析构函数
     */
    ~FrameAllocator();

    // 禁用拷贝和赋值
    FrameAllocator(const FrameAllocator&) = delete;
    FrameAllocator& operator=(const FrameAllocator&) = delete;

    /**
     * @brief 分配视频帧
     * @param width 帧宽度
     * @param height 帧高度
     * @param pixel_format 像素格式
     * @param alignment 内存对齐要求（0表示使用默认）
     * @return 分配结果
     */
    AllocatedFrame allocateFrame(int width, int height, int pixel_format, int alignment = 0);

    /**
     * @brief 释放视频帧
     * @param frame 要释放的帧
     * @return 是否成功释放到池中
     */
    bool deallocateFrame(AVFrame* frame);

    /**
     * @brief 预分配指定规格的帧池
     * @param spec 帧规格
     * @param count 预分配数量
     */
    void preallocateFrames(const FrameSpec& spec, size_t count);

    /**
     * @brief 获取统计信息
     */
    Statistics getStatistics() const { return stats_; }

    /**
     * @brief 获取池信息
     * @return 每个池的详细信息
     */
    std::vector<std::pair<FrameSpec, size_t>> getPoolInfo() const;

    /**
     * @brief 清理空闲的池
     * 移除长时间未使用的帧池以释放内存
     */
    void cleanup();

    /**
     * @brief 设置内存压力回调
     * 当内存使用过高时触发
     */
    void setMemoryPressureCallback(std::function<void(size_t current, size_t peak)> callback);

    /**
     * @brief 强制垃圾回收
     * 立即清理所有空闲帧
     */
    void forceGarbageCollection();

    /**
     * @brief 获取推荐的帧规格
     * 基于历史使用模式推荐最优配置
     */
    std::vector<FrameSpec> getRecommendedSpecs() const;

private:
    /**
     * @brief 获取或创建帧池
     */
    std::shared_ptr<FramePool> getOrCreatePool(const FrameSpec& spec);

    /**
     * @brief 计算帧缓冲区大小
     */
    size_t calculateFrameSize(const FrameSpec& spec) const;

    /**
     * @brief 检查内存压力
     */
    void checkMemoryPressure();

    /**
     * @brief 更新统计信息
     */
    void updateStatistics(bool from_pool, size_t frame_size, bool is_allocation);

private:
    Config config_;                                                    // 配置信息
    mutable Statistics stats_;                                         // 统计信息

    mutable std::mutex pools_mutex_;                                   // 池容器访问锁
    std::unordered_map<FrameSpec, std::shared_ptr<FramePool>> pools_; // 帧池映射

    std::function<void(size_t, size_t)> memory_pressure_callback_;     // 内存压力回调

    std::atomic<bool> shutdown_{false};                               // 关闭标志
};

/**
 * @brief FrameSpec的哈希函数
 */
struct FrameSpecHash {
    size_t operator()(const FrameAllocator::FrameSpec& spec) const {
        size_t h1 = std::hash<int>{}(spec.width);
        size_t h2 = std::hash<int>{}(spec.height);
        size_t h3 = std::hash<int>{}(spec.pixel_format);
        size_t h4 = std::hash<int>{}(spec.alignment);
        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
    }
};

} // namespace std

// 为unordered_map提供FrameSpec的哈希支持
namespace std {
template<>
struct hash<FrameAllocator::FrameSpec> {
    size_t operator()(const FrameAllocator::FrameSpec& spec) const {
        return FrameSpecHash{}(spec);
    }
};
}

/**
 * @brief 全局帧分配器实例
 * 提供全局访问接口
 */
class GlobalFrameAllocator {
public:
    static FrameAllocator& getInstance() {
        static FrameAllocator instance;
        return instance;
    }

    static void configure(const FrameAllocator::Config& config) {
        getInstance() = FrameAllocator(config);
    }

private:
    GlobalFrameAllocator() = default;
};

/**
 * @brief 便捷的帧分配宏
 */
#define ALLOCATE_FRAME(width, height, format) \
GlobalFrameAllocator::getInstance().allocateFrame(width, height, format)

#define DEALLOCATE_FRAME(frame) \
    GlobalFrameAllocator::getInstance().deallocateFrame(frame)

#endif // FRAME_ALLOCATOR_H

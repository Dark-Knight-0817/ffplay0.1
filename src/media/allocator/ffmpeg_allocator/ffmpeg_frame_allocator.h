// ffmpeg_frame_allocator.h - FFmpeg后端实现
#ifndef FFMPEG_FRAME_ALLOCATOR_H
#define FFMPEG_FRAME_ALLOCATOR_H

#include "../frame_allocator_base.h"  // 使用正确的相对路径
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <chrono>

// FFmpeg头文件 - 只在FFmpeg实现中包含
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libavutil/version.h>
}

namespace media {

/**
 * @brief FFmpeg特定的配置
 */
struct FFmpegAllocatorConfig : public AllocatorConfig {
    bool use_av_malloc = true;          // 是否使用av_malloc进行分配
    bool enable_hwaccel = false;        // 是否启用硬件加速内存
    size_t cleanup_interval_ms = 30000; // 清理间隔 (30秒)
    double pool_utilization_threshold = 0.1; // 池利用率阈值，低于此值将被清理
    bool enable_pooling = true;         // 是否启用池化

    FFmpegAllocatorConfig() {
        // FFmpeg特定的默认值
        default_alignment = 32;  // AVX对齐
        max_frame_size = 64 * 1024 * 1024;  // 64MB最大帧
    }
};

/**
 * @brief FFmpeg帧分配器实现
 */
class FFmpegFrameAllocator : public IFrameAllocator {
public:
    explicit FFmpegFrameAllocator(std::unique_ptr<AllocatorConfig> config = nullptr);
    ~FFmpegFrameAllocator() override;

    // 实现IFrameAllocator接口
    AllocatedFrame allocateFrame(const FrameSpec& spec) override;
    bool deallocateFrame(std::unique_ptr<FrameData> frame) override;
    void preallocateFrames(const FrameSpec& spec, size_t count) override;
    
    Statistics getStatistics() const override;
    std::string getBackendName() const override;
    std::vector<std::pair<FrameSpec, size_t>> getPoolInfo() const override;
    void cleanup() override;
    void setMemoryPressureCallback(std::function<void(size_t, size_t)> callback) override;
    
    std::vector<int> getSupportedFormats() const override;
    bool isFormatSupported(int format) const override;
    size_t calculateFrameSize(const FrameSpec& spec) const override;
    void forceGarbageCollection() override;
    std::vector<FrameSpec> getRecommendedSpecs() const override;

    // FFmpeg特有的方法
    /**
     * @brief 直接分配FFmpeg原生帧
     * @param spec 帧规格
     * @return AVFrame指针，失败返回nullptr
     */
    AVFrame* allocateNativeFrame(const FrameSpec& spec);

    /**
     * @brief 释放FFmpeg原生帧
     * @param frame 要释放的帧，成功后会被设置为nullptr
     * @return 是否成功释放到池中
     */
    bool deallocateNativeFrame(AVFrame*& frame);

    /**
     * @brief 将FrameSpec转换为FFmpeg像素格式
     * @param spec 帧规格
     * @return FFmpeg像素格式
     */
    static AVPixelFormat specToPixelFormat(const FrameSpec& spec);

    /**
     * @brief 将FFmpeg像素格式转换为FrameSpec格式
     * @param format FFmpeg像素格式
     * @return 通用格式代码
     */
    static int pixelFormatToSpec(AVPixelFormat format);

    /**
     * @brief 获取FFmpeg版本信息
     * @return 版本字符串
     */
    static std::string getFFmpegVersion();

private:
    /**
     * @brief FFmpeg帧池实现
     */
    class FFmpegFramePool {
    public:
        explicit FFmpegFramePool(const FrameSpec& spec, size_t capacity, const FFmpegAllocatorConfig& config);
        ~FFmpegFramePool();

        // 获取/释放帧
        AVFrame* acquire();
        bool release(AVFrame* frame);

        // 池信息
        size_t available() const { 
            std::lock_guard<std::mutex> lock(mutex_);
            return available_frames_.size(); 
        }
        size_t capacity() const { return capacity_; }
        const FrameSpec& getSpec() const { return spec_; }
        
        // 统计信息
        size_t getTotalAllocated() const { return total_allocated_.load(); }
        size_t getMemoryUsage() const;
        std::chrono::steady_clock::time_point getLastUsed() const { return last_used_; }
        
        // 池管理
        void shrink(size_t new_capacity);
        double getUtilizationRate() const;
        bool shouldCleanup(double threshold, std::chrono::milliseconds max_idle) const;

    private:
        FrameSpec spec_;
        size_t capacity_;
        FFmpegAllocatorConfig config_;
        
        std::vector<AVFrame*> available_frames_;
        mutable std::mutex mutex_;
        std::atomic<size_t> total_allocated_{0};
        std::chrono::steady_clock::time_point last_used_;
        
        // 内部方法
        AVFrame* createFrame();
        void destroyFrame(AVFrame* frame);
        size_t calculateSingleFrameSize() const;
    };

    // 内部方法
    std::shared_ptr<FFmpegFramePool> getOrCreatePool(const FrameSpec& spec);
    std::unique_ptr<FrameData> wrapAVFrame(AVFrame* av_frame, const FrameSpec& spec, bool from_pool);
    AVFrame* unwrapAVFrame(const FrameData* frame_data);
    void updateStatistics(bool from_pool, size_t frame_size, bool is_allocation);
    void checkMemoryPressure();
    void performScheduledCleanup();
    
    // 格式支持检查
    bool isValidFormat(AVPixelFormat format) const;
    std::vector<AVPixelFormat> getSupportedAVFormats() const;

private:
    FFmpegAllocatorConfig config_;
    
    // 池管理
    mutable std::shared_mutex pools_mutex_;  // 读写锁，提升并发性能
    std::unordered_map<FrameSpec, std::shared_ptr<FFmpegFramePool>, FrameSpecHash> pools_;
    
    // 统计信息 (线程安全)
    mutable std::atomic<size_t> total_allocated_{0};
    mutable std::atomic<size_t> total_freed_{0};
    mutable std::atomic<size_t> pool_hits_{0};
    mutable std::atomic<size_t> pool_misses_{0};
    mutable std::atomic<size_t> active_pools_{0};
    mutable std::atomic<size_t> total_memory_usage_{0};
    mutable std::atomic<size_t> peak_memory_usage_{0};
    
    // 回调函数
    std::function<void(size_t, size_t)> memory_pressure_callback_;
    
    // 生命周期管理
    std::atomic<bool> shutdown_{false};
    
    // 清理任务
    mutable std::mutex cleanup_mutex_;
    std::chrono::steady_clock::time_point last_cleanup_;
};

/**
 * @brief FFmpeg像素格式常量
 * 
 * 提供常用像素格式的便捷访问
 */
namespace FFmpegFormats {
    constexpr int YUV420P = AV_PIX_FMT_YUV420P;
    constexpr int YUV422P = AV_PIX_FMT_YUV422P;
    constexpr int YUV444P = AV_PIX_FMT_YUV444P;
    constexpr int RGB24 = AV_PIX_FMT_RGB24;
    constexpr int BGR24 = AV_PIX_FMT_BGR24;
    constexpr int RGBA = AV_PIX_FMT_RGBA;
    constexpr int BGRA = AV_PIX_FMT_BGRA;
    constexpr int NV12 = AV_PIX_FMT_NV12;
    constexpr int NV21 = AV_PIX_FMT_NV21;
    constexpr int GRAY8 = AV_PIX_FMT_GRAY8;
    constexpr int GRAY16LE = AV_PIX_FMT_GRAY16LE;
}

// /**
//  * @brief FFmpeg帧分配器工厂函数
//  */
// std::unique_ptr<IFrameAllocator> createFFmpegFrameAllocator(
//     std::unique_ptr<AllocatorConfig> config = nullptr);

/**
 * @brief 便捷的FFmpeg帧分配宏
 */
#define ALLOCATE_FFMPEG_FRAME(width, height, format) \
    ([&]() { \
        media::FrameSpec spec(width, height, format); \
        auto allocator = media::createFFmpegFrameAllocator(); \
        return allocator->allocateFrame(spec); \
    })()

#define ALLOCATE_YUV420P_FRAME(width, height) \
    ALLOCATE_FFMPEG_FRAME(width, height, media::FFmpegFormats::YUV420P)

#define ALLOCATE_RGB24_FRAME(width, height) \
    ALLOCATE_FFMPEG_FRAME(width, height, media::FFmpegFormats::RGB24)

} // namespace media

/**
 * @brief FFmpeg帧分配器工厂函数
 */
std::unique_ptr<media::IFrameAllocator> createFFmpegFrameAllocator(
    std::unique_ptr<media::AllocatorConfig> config = nullptr);

#endif // FFMPEG_FRAME_ALLOCATOR_H

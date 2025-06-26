#ifndef SMART_POINTERS_H
#define SMART_POINTERS_H

#include <memory>
#include <functional>
#include <vector>
#include <mutex>

// FFmpeg 头文件
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/channel_layout.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

/**
 * @brief FFmpeg资源的智能指针封装
 *
 * 设计特点：
 * 1. RAII管理：自动释放FFmpeg资源，避免内存泄漏
 * 2. 类型安全：为不同FFmpeg类型提供专用智能指针
 * 3. 便捷接口：提供工厂方法和转换函数
 * 4. 线程安全：支持多线程环境下的资源共享
 * 5. 性能优化：减少不必要的拷贝和分配
 * 6. FFmpeg 7.x兼容：支持新的API变更
 */
namespace ffmpeg {

    /**
     * @brief 自定义删除器类
     * 为不同的FFmpeg类型提供正确的释放方法
     */
    namespace deleters {

        /**
         * @brief AVFrame删除器
         */
    struct AVFrameDeleter {
        void operator()(AVFrame* frame) const {
            if (frame) {
                av_frame_free(&frame);
            }
        }
    };

    /**
     * @brief AVPacket删除器
     */
    struct AVPacketDeleter {
        void operator()(AVPacket* packet) const {
            if (packet) {
                av_packet_free(&packet);
            }
        }
    };

    /**
     * @brief AVFormatContext删除器
     */
    struct AVFormatContextDeleter {
        void operator()(AVFormatContext* ctx) const {
            if (ctx) {
                avformat_close_input(&ctx);
            }
        }
    };

    /**
     * @brief AVCodecContext删除器
     */
    struct AVCodecContextDeleter {
        void operator()(AVCodecContext* ctx) const {
            if (ctx) {
                avcodec_free_context(&ctx);
            }
        }
    };

    /**
     * @brief SwsContext删除器
     */
    struct SwsContextDeleter {
        void operator()(SwsContext* ctx) const {
            if (ctx) {
                sws_freeContext(ctx);
            }
        }
    };

    /**
     * @brief SwrContext删除器
     */
    struct SwrContextDeleter {
        void operator()(SwrContext* ctx) const {
            if (ctx) {
                swr_free(&ctx);
            }
        }
    };

    /**
     * @brief 通用内存删除器（用于av_malloc分配的内存）
     */
    struct AVMemoryDeleter {
        void operator()(void* ptr) const {
            if (ptr) {
                av_free(ptr);
            }
        }
    };


} // namespace deleters

/**
 * @brief 智能指针类型定义
 * 这里都是智能指针的用法，
 *  unique_ptr<T, D> up();
 *  空的unique_ptr，接受一个D类型的删除器D，使用D释放内存
 */
using AVFramePtr = std::unique_ptr<AVFrame, deleters::AVFrameDeleter>;
using AVPacketPtr = std::unique_ptr<AVPacket, deleters::AVPacketDeleter>;
using AVFormatContextPtr = std::unique_ptr<AVFormatContext, deleters::AVFormatContextDeleter>;
using AVCodecContextPtr = std::unique_ptr<AVCodecContext, deleters::AVCodecContextDeleter>;
using SwsContextPtr = std::unique_ptr<SwsContext, deleters::SwsContextDeleter>;
using SwrContextPtr = std::unique_ptr<SwrContext, deleters::SwrContextDeleter>;
using AVMemoryPtr = std::unique_ptr<void, deleters::AVMemoryDeleter>;

// 共享智能指针（用于需要共享的资源）
using AVFrameSharedPtr = std::shared_ptr<AVFrame>;
using AVPacketSharedPtr = std::shared_ptr<AVPacket>;

/**
 * @brief 智能指针工厂类
 * 提供便捷的智能指针创建方法
 */
class SmartPointerFactory {
public:
    /**
     * @brief 创建AVFrame智能指针
     * @return 新分配的AVFrame智能指针
     */
    static AVFramePtr createFrame() {
        AVFrame* frame = av_frame_alloc();
        return AVFramePtr(frame);
    }

    /**
     * @brief 创建AVPacket智能指针
     * @return 新分配的AVPacket智能指针
     */
    static AVPacketPtr createPacket() {
        AVPacket* packet = av_packet_alloc();
        return AVPacketPtr(packet);
    }

    /**
     * @brief 创建AVCodecContext智能指针
     * @param codec 编解码器
     * @return 新分配的AVCodecContext智能指针
     */
    static AVCodecContextPtr createCodecContext(const AVCodec* codec) {
        AVCodecContext* ctx = avcodec_alloc_context3(codec);
        return AVCodecContextPtr(ctx);
    }

    /**
     * @brief 创建SwsContext智能指针
     * @param srcW 源宽度
     * @param srcH 源高度
     * @param srcFormat 源像素格式
     * @param dstW 目标宽度
     * @param dstH 目标高度
     * @param dstFormat 目标像素格式
     * @param flags 缩放算法标志
     * @return SwsContext智能指针
     */
    static SwsContextPtr createSwsContext(int srcW, int srcH, AVPixelFormat srcFormat,
                                          int dstW, int dstH, AVPixelFormat dstFormat,
                                          int flags = SWS_BILINEAR) {
        SwsContext* ctx = sws_getContext(srcW, srcH, srcFormat,
                                         dstW, dstH, dstFormat,
                                         flags, nullptr, nullptr, nullptr);
        return SwsContextPtr(ctx);
    }

    /**
     * @brief 创建SwrContext智能指针（FFmpeg 7.x新API）
     * @param out_ch_layout 输出声道布局
     * @param out_sample_fmt 输出采样格式
     * @param out_sample_rate 输出采样率
     * @param in_ch_layout 输入声道布局
     * @param in_sample_fmt 输入采样格式
     * @param in_sample_rate 输入采样率
     * @return SwrContext智能指针
     */
    static SwrContextPtr createSwrContext(const AVChannelLayout* out_ch_layout, AVSampleFormat out_sample_fmt, int out_sample_rate,
                                          const AVChannelLayout* in_ch_layout, AVSampleFormat in_sample_fmt, int in_sample_rate) {
        SwrContext* ctx = nullptr;
        int ret = swr_alloc_set_opts2(&ctx,
                                      out_ch_layout, out_sample_fmt, out_sample_rate,
                                      in_ch_layout, in_sample_fmt, in_sample_rate,
                                      0, nullptr);
        if (ret < 0 || !ctx) {
            return SwrContextPtr(nullptr);
        }

        if (swr_init(ctx) < 0) {
            swr_free(&ctx);
            return SwrContextPtr(nullptr);
        }
        return SwrContextPtr(ctx);
    }

    /**
     * @brief 创建SwrContext智能指针（便捷方法，使用标准声道布局）
     * @param out_channels 输出声道数
     * @param out_sample_fmt 输出采样格式
     * @param out_sample_rate 输出采样率
     * @param in_channels 输入声道数
     * @param in_sample_fmt 输入采样格式
     * @param in_sample_rate 输入采样率
     * @return SwrContext智能指针
     */
    static SwrContextPtr createSwrContextSimple(int out_channels, AVSampleFormat out_sample_fmt, int out_sample_rate,
                                                int in_channels, AVSampleFormat in_sample_fmt, int in_sample_rate) {
        AVChannelLayout out_layout, in_layout;

        // 使用默认声道布局
        av_channel_layout_default(&out_layout, out_channels);
        av_channel_layout_default(&in_layout, in_channels);

        auto ctx = createSwrContext(&out_layout, out_sample_fmt, out_sample_rate,
                                    &in_layout, in_sample_fmt, in_sample_rate);

        // 清理临时的声道布局
        av_channel_layout_uninit(&out_layout);
        av_channel_layout_uninit(&in_layout);

        return ctx;
    }

    /**
     * @brief 分配图像缓冲区
     * @param frame 要分配缓冲区的帧
     * @param pix_fmt 像素格式
     * @param width 宽度
     * @param height 高度
     * @param align 内存对齐
     * @return 分配是否成功
     */
    static bool allocateImageBuffer(AVFrame* frame, AVPixelFormat pix_fmt,
                                    int width, int height, int align = 32) {
        if (!frame) return false;

        frame->format = pix_fmt;
        frame->width = width;
        frame->height = height;

        return av_frame_get_buffer(frame, align) >= 0;
    }

    /**
     * @brief 分配音频缓冲区（FFmpeg 7.x新API）
     * @param frame 要分配缓冲区的帧
     * @param sample_fmt 采样格式
     * @param nb_samples 采样数
     * @param ch_layout 声道布局
     * @param align 内存对齐
     * @return 分配是否成功
     */
    static bool allocateAudioBuffer(AVFrame* frame, AVSampleFormat sample_fmt,
                                    int nb_samples, const AVChannelLayout* ch_layout, int align = 0) {
        if (!frame || !ch_layout) return false;

        frame->format = sample_fmt;
        frame->nb_samples = nb_samples;
        if (av_channel_layout_copy(&frame->ch_layout, ch_layout) < 0) {
            return false;
        }

        return av_frame_get_buffer(frame, align) >= 0;
    }

    /**
     * @brief 分配音频缓冲区（便捷方法）
     * @param frame 要分配缓冲区的帧
     * @param sample_fmt 采样格式
     * @param nb_samples 采样数
     * @param channels 声道数
     * @param align 内存对齐
     * @return 分配是否成功
     */
    static bool allocateAudioBufferSimple(AVFrame* frame, AVSampleFormat sample_fmt,
                                          int nb_samples, int channels, int align = 0) {
        if (!frame) return false;

        AVChannelLayout ch_layout;
        av_channel_layout_default(&ch_layout, channels);

        bool result = allocateAudioBuffer(frame, sample_fmt, nb_samples, &ch_layout, align);

        av_channel_layout_uninit(&ch_layout);
        return result;
    }
};

/**
 * @brief 共享智能指针工厂
 * 用于创建可以在多个地方共享的FFmpeg资源
 */
class SharedPointerFactory {
public:
    /**
     * @brief 创建共享AVFrame智能指针
     * @return 新分配的共享AVFrame智能指针
     */
    static AVFrameSharedPtr createSharedFrame() {
        AVFrame* frame = av_frame_alloc();
        return AVFrameSharedPtr(frame, deleters::AVFrameDeleter{});
    }

    /**
     * @brief 创建共享AVPacket智能指针
     * @return 新分配的共享AVPacket智能指针
     */
    static AVPacketSharedPtr createSharedPacket() {
        AVPacket* packet = av_packet_alloc();
        return AVPacketSharedPtr(packet, deleters::AVPacketDeleter{});
    }

    /**
     * @brief 从原始指针创建共享智能指针
     * @param frame 原始AVFrame指针
     * @return 共享智能指针（注意：会接管指针的所有权）
     */
    static AVFrameSharedPtr wrapFrame(AVFrame* frame) {
        return AVFrameSharedPtr(frame, deleters::AVFrameDeleter{});
    }

    /**
     * @brief 从原始指针创建共享智能指针
     * @param packet 原始AVPacket指针
     * @return 共享智能指针（注意：会接管指针的所有权）
     */
    static AVPacketSharedPtr wrapPacket(AVPacket* packet) {
        return AVPacketSharedPtr(packet, deleters::AVPacketDeleter{});
    }
};

/**
 * @brief 智能指针转换工具
 * 提供智能指针与原始指针之间的转换
 */
class SmartPointerConverter {
public:
    /**
     * @brief 从智能指针获取原始指针（不转移所有权）
     */
    template<typename T, typename D>
    static T* getRaw(const std::unique_ptr<T, D>& ptr) {
        return ptr.get();
    }

    template<typename T>
    static T* getRaw(const std::shared_ptr<T>& ptr) {
        return ptr.get();
    }

    /**
     * @brief 释放智能指针的所有权并返回原始指针
     * 调用者负责手动释放返回的指针
     */
    template<typename T, typename D>
    static T* release(std::unique_ptr<T, D>& ptr) {
        return ptr.release();
    }

    /**
     * @brief 从原始指针创建智能指针（接管所有权）
     */
    static AVFramePtr wrapFrame(AVFrame* frame) {
        return AVFramePtr(frame);
    }

    static AVPacketPtr wrapPacket(AVPacket* packet) {
        return AVPacketPtr(packet);
    }

    static AVFormatContextPtr wrapFormatContext(AVFormatContext* ctx) {
        return AVFormatContextPtr(ctx);
    }

    static AVCodecContextPtr wrapCodecContext(AVCodecContext* ctx) {
        return AVCodecContextPtr(ctx);
    }
};

/**
 * @brief 引用计数帧（用于零拷贝优化）
 * 封装了AVFrame的引用计数机制
 */
class RefCountedFrame {
public:
    explicit RefCountedFrame(AVFramePtr frame) : frame_(std::move(frame)) {}

    /**
     * @brief 创建当前帧的引用
     * @return 新的引用（零拷贝）
     */
    AVFramePtr createRef() const {
        if (!frame_) return nullptr;

        AVFrame* new_frame = av_frame_alloc();
        if (!new_frame) return nullptr;

        if (av_frame_ref(new_frame, frame_.get()) < 0) {
            av_frame_free(&new_frame);
            return nullptr;
        }

        return AVFramePtr(new_frame);
    }

    /**
     * @brief 克隆帧（深拷贝）
     * @return 新的帧拷贝
     */
    AVFramePtr clone() const {
        if (!frame_) return nullptr;

        AVFrame* new_frame = av_frame_alloc();
        if (!new_frame) return nullptr;

        if (av_frame_ref(new_frame, frame_.get()) < 0) {
            av_frame_free(&new_frame);
            return nullptr;
        }

        // 确保缓冲区是可写的
        if (av_frame_make_writable(new_frame) < 0) {
            av_frame_free(&new_frame);
            return nullptr;
        }

        return AVFramePtr(new_frame);
    }

    /**
     * @brief 检查帧是否可写
     */
    bool isWritable() const {
        return frame_ && av_frame_is_writable(frame_.get());
    }

    /**
     * @brief 确保帧是可写的
     */
    bool makeWritable() {
        if (!frame_) return false;
        return av_frame_make_writable(frame_.get()) >= 0;
    }

    AVFrame* get() const { return frame_.get(); }
    AVFrame* operator->() const { return frame_.get(); }
    AVFrame& operator*() const { return *frame_.get(); }

private:
    AVFramePtr frame_;
};

/**
 * @brief 音视频帧池
 * 使用对象池模式管理AVFrame，减少分配开销
 */
template<size_t PoolSize = 16>
class FramePool {
public:
    FramePool() {
        for (size_t i = 0; i < PoolSize; ++i) {
            pool_.push_back(SmartPointerFactory::createFrame());
        }
    }

    /**
     * @brief 从池中获取一个帧
     * @return 可用的帧，如果池为空则创建新帧
     */
    AVFramePtr acquire() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (!pool_.empty()) {
            auto frame = std::move(pool_.back());
            pool_.pop_back();

            // 重置帧数据
            av_frame_unref(frame.get());
            return frame;
        }

        // 池为空，创建新帧
        return SmartPointerFactory::createFrame();
    }

    /**
     * @brief 归还帧到池中
     * @param frame 要归还的帧
     */
    void release(AVFramePtr frame) {
        if (!frame) return;

        std::lock_guard<std::mutex> lock(mutex_);

        if (pool_.size() < PoolSize) {
            av_frame_unref(frame.get());  // 清理帧数据
            pool_.push_back(std::move(frame));
        }
        // 如果池已满，frame会自动销毁
    }

    /**
     * @brief 获取池中可用帧数量
     */
    size_t available() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return pool_.size();
    }

private:
    mutable std::mutex mutex_;
    std::vector<AVFramePtr> pool_;
};

/**
 * @brief 全局帧池实例
 */
extern FramePool<32> g_video_frame_pool;
extern FramePool<16> g_audio_frame_pool;

/**
 * @brief 便捷的宏定义
 * 简化智能指针的使用
 */
#define MAKE_FRAME() ffmpeg::SmartPointerFactory::createFrame()
#define MAKE_PACKET() ffmpeg::SmartPointerFactory::createPacket()
#define MAKE_SHARED_FRAME() ffmpeg::SharedPointerFactory::createSharedFrame()
#define MAKE_SHARED_PACKET() ffmpeg::SharedPointerFactory::createSharedPacket()

#define ACQUIRE_FRAME() ffmpeg::g_video_frame_pool.acquire()
#define RELEASE_FRAME(frame) ffmpeg::g_video_frame_pool.release(std::move(frame))

/**
 * @brief RAII辅助类
 * 用于自动管理临时资源
 */
template<typename T, typename Deleter>
class ScopedResource {
public:
    explicit ScopedResource(T* resource) : resource_(resource) {}

    ~ScopedResource() {
        if (resource_) {
            Deleter{}(resource_);
        }
    }

    T* get() const { return resource_; }
    T* operator->() const { return resource_; }
    T& operator*() const { return *resource_; }

    T* release() {
        T* temp = resource_;
        resource_ = nullptr;
        return temp;
    }

    // 禁用拷贝
    ScopedResource(const ScopedResource&) = delete;
    ScopedResource& operator=(const ScopedResource&) = delete;

    // 支持移动
    ScopedResource(ScopedResource&& other) noexcept : resource_(other.resource_) {
        other.resource_ = nullptr;
    }

    ScopedResource& operator=(ScopedResource&& other) noexcept {
        if (this != &other) {
            if (resource_) {
                Deleter{}(resource_);
            }
            resource_ = other.resource_;
            other.resource_ = nullptr;
        }
        return *this;
    }

private:
    T* resource_;
};

// 常用的RAII类型别名
using ScopedAVFrame = ScopedResource<AVFrame, deleters::AVFrameDeleter>;
using ScopedAVPacket = ScopedResource<AVPacket, deleters::AVPacketDeleter>;

/**
 * @brief 声道布局辅助工具
 * 简化FFmpeg 7.x新API的使用
 */
class ChannelLayoutHelper {
public:
    /**
     * @brief 创建标准声道布局
     */
    static AVChannelLayout createDefault(int channels) {
        AVChannelLayout layout;
        av_channel_layout_default(&layout, channels);
        return layout;
    }

    /**
     * @brief 创建立体声布局
     */
    static AVChannelLayout createStereo() {
        return createDefault(2);
    }

    /**
     * @brief 创建单声道布局
     */
    static AVChannelLayout createMono() {
        return createDefault(1);
    }

    /**
     * @brief 创建5.1声道布局
     */
    static AVChannelLayout create5_1() {
        return createDefault(6);
    }

    /**
     * @brief RAII包装器，自动管理声道布局的生命周期
     */
    class ScopedChannelLayout {
    public:
        explicit ScopedChannelLayout(int channels) {
            av_channel_layout_default(&layout_, channels);
        }

        ~ScopedChannelLayout() {
            av_channel_layout_uninit(&layout_);
        }

        const AVChannelLayout* get() const { return &layout_; }
        AVChannelLayout* get() { return &layout_; }

        // 禁用拷贝
        ScopedChannelLayout(const ScopedChannelLayout&) = delete;
        ScopedChannelLayout& operator=(const ScopedChannelLayout&) = delete;

    private:
        AVChannelLayout layout_;
    };
};

} // namespace ffmpeg

#endif // SMART_POINTERS_H

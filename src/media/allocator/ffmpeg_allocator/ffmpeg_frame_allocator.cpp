// ffmpeg_frame_allocator.cpp - FFmpeg分配器实现
#include "ffmpeg_frame_allocator.h"
#include <algorithm>
#include <chrono>
#include <cstring>

namespace media {

// FFmpegFrameAllocator 实现
FFmpegFrameAllocator::FFmpegFrameAllocator(std::unique_ptr<AllocatorConfig> config) {
    // 转换配置或使用默认配置
    if (config && dynamic_cast<FFmpegAllocatorConfig*>(config.get())) {
        config_ = *static_cast<FFmpegAllocatorConfig*>(config.get());
    } else {
        config_ = FFmpegAllocatorConfig();
    }
    
    last_cleanup_ = std::chrono::steady_clock::now();
}

FFmpegFrameAllocator::~FFmpegFrameAllocator() {
    shutdown_.store(true);
    cleanup();
}

AllocatedFrame FFmpegFrameAllocator::allocateFrame(const FrameSpec& spec) {
    if (shutdown_.load()) {
        throw AllocatorException(AllocatorError::NotInitialized, "Allocator is shut down");
    }

    // 验证格式支持
    AVPixelFormat av_format = specToPixelFormat(spec);
    if (!isValidFormat(av_format)) {
        throw AllocatorException(AllocatorError::UnsupportedFormat, 
            "Unsupported pixel format: " + std::to_string(spec.pixel_format));
    }

    bool from_pool = false;
    AVFrame* av_frame = nullptr;
    
    // 尝试从池中获取
    if (config_.enable_pooling) {
        auto pool = getOrCreatePool(spec);
        if (pool) {
            av_frame = pool->acquire();
            if (av_frame) {
                from_pool = true;
                pool_hits_.fetch_add(1);
            } else {
                pool_misses_.fetch_add(1);
            }
        }
    }

    // 池中没有可用帧，直接分配
    if (!av_frame) {
        av_frame = allocateNativeFrame(spec);
        if (!av_frame) {
            throw AllocatorException(AllocatorError::OutOfMemory, 
                "Failed to allocate FFmpeg frame");
        }
    }

    // 包装为通用帧
    auto frame_data = wrapAVFrame(av_frame, spec, from_pool);
    size_t frame_size = calculateFrameSize(spec);
    
    updateStatistics(from_pool, frame_size, true);
    checkMemoryPressure();

    AllocatedFrame result;
    result.frame = std::move(frame_data);
    result.from_pool = from_pool;
    result.spec = spec;
    result.backend = getBackendName();
    
    return result;
}

bool FFmpegFrameAllocator::deallocateFrame(std::unique_ptr<FrameData> frame) {
    if (!frame || shutdown_.load()) {
        return false;
    }

    AVFrame* av_frame = unwrapAVFrame(frame.get());
    if (!av_frame) {
        return false;
    }

    bool returned_to_pool = false;
    
    // 如果启用池化且帧来自池，尝试归还
    if (config_.enable_pooling) {
        FrameSpec spec(frame->width, frame->height, frame->format);
        auto pool = getOrCreatePool(spec);
        if (pool) {
            returned_to_pool = pool->release(av_frame);
        }
    }

    // 如果无法归还到池，直接释放
    if (!returned_to_pool) {
        // 释放图像数据缓冲区
        if (av_frame->data[0]) {
            av_freep(&av_frame->data[0]);
        }
        av_frame_free(&av_frame);
    }

    size_t frame_size = calculateFrameSize(FrameSpec(frame->width, frame->height, frame->format));
    updateStatistics(returned_to_pool, frame_size, false);

    return returned_to_pool;
}

void FFmpegFrameAllocator::preallocateFrames(const FrameSpec& spec, size_t count) {
    if (!config_.enable_pooling || shutdown_.load()) {
        return;
    }
    
    auto pool = getOrCreatePool(spec);
    if (!pool) {
        return;
    }
    
    // 预分配到指定数量
    for (size_t i = pool->available(); i < count && i < pool->capacity(); ++i) {
        AVFrame* frame = allocateNativeFrame(spec);
        if (frame) {
            if (!pool->release(frame)) {
                // 释放图像数据缓冲区
                if (frame->data[0]) {
                    av_freep(&frame->data[0]);
                }
                av_frame_free(&frame);
                break;
            }
        }
    }
}

Statistics FFmpegFrameAllocator::getStatistics() const {
    Statistics stats;
    stats.backend = getBackendName();
    stats.total_allocated = total_allocated_.load();
    stats.total_freed = total_freed_.load();
    stats.pool_hits = pool_hits_.load();
    stats.pool_misses = pool_misses_.load();
    stats.active_pools = active_pools_.load();
    stats.total_memory_usage = total_memory_usage_.load();
    stats.peak_memory_usage = peak_memory_usage_.load();
    return stats;
}

std::string FFmpegFrameAllocator::getBackendName() const {
    return "FFmpeg";
}

std::vector<std::pair<FrameSpec, size_t>> FFmpegFrameAllocator::getPoolInfo() const {
    std::vector<std::pair<FrameSpec, size_t>> info;
    
    std::shared_lock<std::shared_mutex> lock(pools_mutex_);
    for (const auto& pair : pools_) {
        info.emplace_back(pair.first, pair.second->available());
    }
    
    return info;
}

void FFmpegFrameAllocator::cleanup() {
    performScheduledCleanup();
}

void FFmpegFrameAllocator::setMemoryPressureCallback(
    std::function<void(size_t, size_t)> callback) {
    memory_pressure_callback_ = callback;
}

std::vector<int> FFmpegFrameAllocator::getSupportedFormats() const {
    std::vector<int> formats;
    auto av_formats = getSupportedAVFormats();
    
    for (AVPixelFormat fmt : av_formats) {
        formats.push_back(pixelFormatToSpec(fmt));
    }
    
    return formats;
}

bool FFmpegFrameAllocator::isFormatSupported(int format) const {
    AVPixelFormat av_format = static_cast<AVPixelFormat>(format);
    return isValidFormat(av_format);
}

size_t FFmpegFrameAllocator::calculateFrameSize(const FrameSpec& spec) const {
    AVPixelFormat format = specToPixelFormat(spec);
    
    // 使用FFmpeg的函数计算图像大小
    int size = av_image_get_buffer_size(format, spec.width, spec.height, spec.alignment);
    return size > 0 ? static_cast<size_t>(size) : 0;
}

void FFmpegFrameAllocator::forceGarbageCollection() {
    std::lock_guard<std::mutex> cleanup_lock(cleanup_mutex_);
    
    std::unique_lock<std::shared_mutex> lock(pools_mutex_);
    auto it = pools_.begin();
    
    while (it != pools_.end()) {
        auto& pool = it->second;
        
        // 强制清理所有未使用的池
        if (pool->available() == pool->capacity()) {
            it = pools_.erase(it);
            active_pools_.fetch_sub(1);
        } else {
            // 收缩池到最小大小
            pool->shrink(1);
            ++it;
        }
    }
}

std::vector<FrameSpec> FFmpegFrameAllocator::getRecommendedSpecs() const {
    std::vector<FrameSpec> specs;
    
    // 基于常用分辨率和格式推荐
    std::vector<std::pair<int, int>> resolutions = {
        {1920, 1080}, {1280, 720}, {640, 480}, {320, 240}
    };
    
    std::vector<int> formats = {
        FFmpegFormats::YUV420P,
        FFmpegFormats::RGB24,
        FFmpegFormats::NV12
    };
    
    for (const auto& res : resolutions) {
        for (int fmt : formats) {
            specs.emplace_back(res.first, res.second, fmt);
        }
    }
    
    return specs;
}

// FFmpeg特有的方法实现
AVFrame* FFmpegFrameAllocator::allocateNativeFrame(const FrameSpec& spec) {
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        return nullptr;
    }

    frame->width = spec.width;
    frame->height = spec.height;
    frame->format = specToPixelFormat(spec);

    // 直接使用FFmpeg函数分配缓冲区
    AVPixelFormat format = static_cast<AVPixelFormat>(frame->format);
    int ret = av_image_alloc(frame->data, frame->linesize, 
                           frame->width, frame->height, 
                           format, spec.alignment);
    
    if (ret < 0) {
        av_frame_free(&frame);
        return nullptr;
    }

    return frame;
}

bool FFmpegFrameAllocator::deallocateNativeFrame(AVFrame*& frame) {
    if (!frame) {
        return false;
    }

    // 尝试归还到池
    FrameSpec spec(frame->width, frame->height, pixelFormatToSpec(static_cast<AVPixelFormat>(frame->format)));
    
    if (config_.enable_pooling) {
        auto pool = getOrCreatePool(spec);
        if (pool && pool->release(frame)) {
            frame = nullptr;
            return true;
        }
    }

    // 直接释放 - 释放图像数据缓冲区
    if (frame->data[0]) {
        av_freep(&frame->data[0]);
    }
    av_frame_free(&frame);
    return false;
}

AVPixelFormat FFmpegFrameAllocator::specToPixelFormat(const FrameSpec& spec) {
    return static_cast<AVPixelFormat>(spec.pixel_format);
}

int FFmpegFrameAllocator::pixelFormatToSpec(AVPixelFormat format) {
    return static_cast<int>(format);
}

std::string FFmpegFrameAllocator::getFFmpegVersion() {
    return std::to_string(LIBAVUTIL_VERSION_MAJOR) + "." + 
           std::to_string(LIBAVUTIL_VERSION_MINOR) + "." + 
           std::to_string(LIBAVUTIL_VERSION_MICRO);
}

// 私有方法实现
std::shared_ptr<FFmpegFrameAllocator::FFmpegFramePool> 
FFmpegFrameAllocator::getOrCreatePool(const FrameSpec& spec) {
    // 先尝试读锁
    {
        std::shared_lock<std::shared_mutex> lock(pools_mutex_);
        auto it = pools_.find(spec);
        if (it != pools_.end()) {
            return it->second;
        }
    }
    
    // 需要创建新池，获取写锁
    std::unique_lock<std::shared_mutex> lock(pools_mutex_);
    
    // 双重检查
    auto it = pools_.find(spec);
    if (it != pools_.end()) {
        return it->second;
    }
    
    // 检查池数量限制
    if (pools_.size() >= config_.max_pools) {
        return nullptr;
    }
    
    // 创建新池
    auto pool = std::make_shared<FFmpegFramePool>(spec, config_.frames_per_pool, config_);
    pools_[spec] = pool;
    active_pools_.fetch_add(1);
    
    return pool;
}

std::unique_ptr<FrameData> FFmpegFrameAllocator::wrapAVFrame(
    AVFrame* av_frame, const FrameSpec& spec, bool from_pool) {
    
    auto frame_data = std::make_unique<FrameData>();
    
    frame_data->width = av_frame->width;
    frame_data->height = av_frame->height;
    frame_data->format = pixelFormatToSpec(static_cast<AVPixelFormat>(av_frame->format));
    frame_data->native_frame = av_frame;
    frame_data->buffer_size = calculateFrameSize(spec);
    
    // 复制平面数据指针和行字节数
    for (int i = 0; i < 4; ++i) {
        frame_data->data[i] = av_frame->data[i];
        frame_data->linesize[i] = av_frame->linesize[i];
    }
    
    return frame_data;
}

AVFrame* FFmpegFrameAllocator::unwrapAVFrame(const FrameData* frame_data) {
    return static_cast<AVFrame*>(frame_data->native_frame);
}

void FFmpegFrameAllocator::updateStatistics(bool from_pool, size_t frame_size, bool is_allocation) {
    if (is_allocation) {
        total_allocated_.fetch_add(1);
        
        size_t new_usage = total_memory_usage_.fetch_add(frame_size) + frame_size;
        
        // 更新峰值使用量
        size_t current_peak = peak_memory_usage_.load();
        while (new_usage > current_peak && 
               !peak_memory_usage_.compare_exchange_weak(current_peak, new_usage)) {
            // 继续尝试直到成功
        }
    } else {
        total_freed_.fetch_add(1);
        total_memory_usage_.fetch_sub(frame_size);
    }
}

void FFmpegFrameAllocator::checkMemoryPressure() {
    if (memory_pressure_callback_) {
        size_t current = total_memory_usage_.load();
        size_t peak = peak_memory_usage_.load();
        
        // 如果当前使用量超过峰值的90%，触发回调
        if (current > peak * 0.9) {
            memory_pressure_callback_(current, peak);
        }
    }
}

void FFmpegFrameAllocator::performScheduledCleanup() {
    auto now = std::chrono::steady_clock::now();
    auto interval = std::chrono::milliseconds(config_.cleanup_interval_ms);
    
    std::lock_guard<std::mutex> cleanup_lock(cleanup_mutex_);
    
    if (now - last_cleanup_ < interval) {
        return;
    }
    
    std::unique_lock<std::shared_mutex> lock(pools_mutex_);
    auto it = pools_.begin();
    
    while (it != pools_.end()) {
        auto& pool = it->second;
        
        if (pool->shouldCleanup(config_.pool_utilization_threshold, interval)) {
            it = pools_.erase(it);
            active_pools_.fetch_sub(1);
        } else {
            ++it;
        }
    }
    
    last_cleanup_ = now;
}

bool FFmpegFrameAllocator::isValidFormat(AVPixelFormat format) const {
    auto supported = getSupportedAVFormats();
    return std::find(supported.begin(), supported.end(), format) != supported.end();
}

std::vector<AVPixelFormat> FFmpegFrameAllocator::getSupportedAVFormats() const {
    return {
        AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_RGB24,
        AV_PIX_FMT_BGR24,
        AV_PIX_FMT_RGBA,
        AV_PIX_FMT_BGRA,
        AV_PIX_FMT_NV12,
        AV_PIX_FMT_NV21,
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_GRAY16LE
    };
}

// FFmpegFramePool 实现
FFmpegFrameAllocator::FFmpegFramePool::FFmpegFramePool(
    const FrameSpec& spec, size_t capacity, const FFmpegAllocatorConfig& config)
    : spec_(spec), capacity_(capacity), config_(config) {
    
    available_frames_.reserve(capacity);
    last_used_ = std::chrono::steady_clock::now();
}

FFmpegFrameAllocator::FFmpegFramePool::~FFmpegFramePool() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (AVFrame* frame : available_frames_) {
        destroyFrame(frame);
    }
    available_frames_.clear();
}

AVFrame* FFmpegFrameAllocator::FFmpegFramePool::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (available_frames_.empty()) {
        return nullptr;
    }
    
    AVFrame* frame = available_frames_.back();
    available_frames_.pop_back();
    last_used_ = std::chrono::steady_clock::now();
    
    return frame;
}

bool FFmpegFrameAllocator::FFmpegFramePool::release(AVFrame* frame) {
    if (!frame) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 检查容量限制
    if (available_frames_.size() >= capacity_) {
        return false;
    }
    
    // 验证帧规格匹配
    if (frame->width != spec_.width || 
        frame->height != spec_.height || 
        frame->format != static_cast<int>(FFmpegFrameAllocator::specToPixelFormat(spec_))) {
        return false;
    }
    
    available_frames_.push_back(frame);
    last_used_ = std::chrono::steady_clock::now();
    
    return true;
}

size_t FFmpegFrameAllocator::FFmpegFramePool::getMemoryUsage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return available_frames_.size() * calculateSingleFrameSize();
}

void FFmpegFrameAllocator::FFmpegFramePool::shrink(size_t new_capacity) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (new_capacity >= capacity_) {
        return;
    }
    
    // 释放多余的帧
    while (available_frames_.size() > new_capacity) {
        AVFrame* frame = available_frames_.back();
        available_frames_.pop_back();
        destroyFrame(frame);
    }
    
    capacity_ = new_capacity;
}

double FFmpegFrameAllocator::FFmpegFramePool::getUtilizationRate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (capacity_ == 0) {
        return 0.0;
    }
    
    size_t used = capacity_ - available_frames_.size();
    return static_cast<double>(used) / capacity_;
}

bool FFmpegFrameAllocator::FFmpegFramePool::shouldCleanup(
    double threshold, std::chrono::milliseconds max_idle) const {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto now = std::chrono::steady_clock::now();
    bool idle_too_long = (now - last_used_) > max_idle;
    bool utilization_low = getUtilizationRate() < threshold;
    
    return idle_too_long && utilization_low;
}

AVFrame* FFmpegFrameAllocator::FFmpegFramePool::createFrame() {
    AVFrame* frame = av_frame_alloc();
    if (!frame) {
        return nullptr;
    }

    frame->width = spec_.width;
    frame->height = spec_.height;
    frame->format = static_cast<int>(FFmpegFrameAllocator::specToPixelFormat(spec_));

    // 直接使用FFmpeg函数分配缓冲区
    AVPixelFormat format = static_cast<AVPixelFormat>(frame->format);
    int ret = av_image_alloc(frame->data, frame->linesize, 
                           frame->width, frame->height, 
                           format, spec_.alignment);
    
    if (ret < 0) {
        av_frame_free(&frame);
        return nullptr;
    }

    total_allocated_.fetch_add(1);
    return frame;
}

void FFmpegFrameAllocator::FFmpegFramePool::destroyFrame(AVFrame* frame) {
    if (frame) {
        // 释放图像数据缓冲区
        if (frame->data[0]) {
            av_freep(&frame->data[0]);
        }
        // 释放AVFrame结构体
        av_frame_free(&frame);
    }
}

size_t FFmpegFrameAllocator::FFmpegFramePool::calculateSingleFrameSize() const {
    AVPixelFormat format = FFmpegFrameAllocator::specToPixelFormat(spec_);
    int size = av_image_get_buffer_size(format, spec_.width, spec_.height, spec_.alignment);
    return size > 0 ? static_cast<size_t>(size) : 0;
}

// 工厂函数实现
std::unique_ptr<IFrameAllocator> createFFmpegFrameAllocator(
    std::unique_ptr<AllocatorConfig> config) {
    return std::make_unique<FFmpegFrameAllocator>(std::move(config));
}

} // namespace media
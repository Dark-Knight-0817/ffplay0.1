#include "frame_allocator.h"
#include <algorithm>
#include <cstring>

// 条件包含FFmpeg头文件
#ifdef FFMPEG_AVAILABLE
extern "C" {
#include <libavutil/frame.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
}
#endif

// FramePool 实现
FrameAllocator::FramePool::FramePool(const FrameSpec& spec, size_t capacity)
    : spec_(spec), capacity_(capacity) {
    available_frames_.reserve(capacity);
}

FrameAllocator::FramePool::~FramePool() {
    for (AVFrame* frame : available_frames_) {
        destroyFrame(frame);
    }
    available_frames_.clear();
}

AVFrame* FrameAllocator::FramePool::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!available_frames_.empty()) {
        AVFrame* frame = available_frames_.back();
        available_frames_.pop_back();
        return frame;
    }

    // 池为空，创建新帧
    if (total_allocated_.load() < capacity_) {
        return createFrame();
    }

    return nullptr;  // 池已满
}

bool FrameAllocator::FramePool::release(AVFrame* frame) {
    if (!frame) return false;

    std::lock_guard<std::mutex> lock(mutex_);

    if (available_frames_.size() < capacity_) {
#ifdef FFMPEG_AVAILABLE
        // 重置帧数据但保留缓冲区
        av_frame_unref(frame);
#endif
        available_frames_.push_back(frame);
        return true;
    }

    // 池已满，销毁帧
    destroyFrame(frame);
    return false;
}

AVFrame* FrameAllocator::FramePool::createFrame() {
#ifdef FFMPEG_AVAILABLE
    AVFrame* frame = av_frame_alloc();
    if (!frame) return nullptr;

    if (!allocateBuffer(frame)) {
        av_frame_free(&frame);
        return nullptr;
    }

    total_allocated_.fetch_add(1);
    return frame;
#else
    // FFmpeg不可用时的占位符实现
    return nullptr;
#endif
}

void FrameAllocator::FramePool::destroyFrame(AVFrame* frame) {
    if (!frame) return;

#ifdef FFMPEG_AVAILABLE
    av_frame_free(&frame);
#endif

    if (total_allocated_.load() > 0) {
        total_allocated_.fetch_sub(1);
    }
}

bool FrameAllocator::FramePool::allocateBuffer(AVFrame* frame) {
    if (!frame) return false;

#ifdef FFMPEG_AVAILABLE
    frame->format = spec_.pixel_format;
    frame->width = spec_.width;
    frame->height = spec_.height;

    return av_frame_get_buffer(frame, spec_.alignment) >= 0;
#else
    return false;
#endif
}

size_t FrameAllocator::FramePool::getMemoryUsage() const {
#ifdef FFMPEG_AVAILABLE
    // 估算单帧大小
    int buffer_size = av_image_get_buffer_size(
        static_cast<AVPixelFormat>(spec_.pixel_format),
        spec_.width, spec_.height, spec_.alignment);

    if (buffer_size > 0) {
        return total_allocated_.load() * buffer_size;
    }
#endif

    return 0;
}

// FrameAllocator 实现
FrameAllocator::FrameAllocator(const Config& config) : config_(config) {
    // 预分配常见分辨率的池
    std::vector<std::pair<int, int>> common_resolutions = {
        {1920, 1080}, {1280, 720}, {640, 480}, {320, 240}
    };

    for (const auto& res : common_resolutions) {
#ifdef FFMPEG_AVAILABLE
        FrameSpec spec(res.first, res.second, AV_PIX_FMT_YUV420P, config_.default_alignment);
        preallocateFrames(spec, config_.frames_per_pool / 2);
#endif
    }
}

FrameAllocator::~FrameAllocator() {
    shutdown_.store(true);

    std::lock_guard<std::mutex> lock(pools_mutex_);
    pools_.clear();
}

FrameAllocator::AllocatedFrame FrameAllocator::allocateFrame(
    int width, int height, int pixel_format, int alignment) {

    if (shutdown_.load()) {
        return AllocatedFrame{};
    }

    if (alignment == 0) {
        alignment = config_.default_alignment;
    }

    FrameSpec spec(width, height, pixel_format, alignment);

    // 检查帧大小限制
    size_t estimated_size = calculateFrameSize(spec);
    if (estimated_size > config_.max_frame_size) {
        return AllocatedFrame{};
    }

    AllocatedFrame result;
    result.spec = spec;
    result.buffer_size = estimated_size;

    // 尝试从池中获取
    auto pool = getOrCreatePool(spec);
    if (pool) {
        result.frame = pool->acquire();
        if (result.frame) {
            result.from_pool = true;
            updateStatistics(true, estimated_size, true);
            stats_.pool_hits.fetch_add(1);
            return result;
        }
    }

    // 池中无可用帧，直接分配
    stats_.pool_misses.fetch_add(1);

#ifdef FFMPEG_AVAILABLE
    AVFrame* frame = av_frame_alloc();
    if (frame) {
        frame->format = pixel_format;
        frame->width = width;
        frame->height = height;

        if (av_frame_get_buffer(frame, alignment) >= 0) {
            result.frame = frame;
            result.from_pool = false;
            updateStatistics(false, estimated_size, true);
            return result;
        }

        av_frame_free(&frame);
    }
#endif

    return AllocatedFrame{};
}

bool FrameAllocator::deallocateFrame(AVFrame* frame) {
    if (!frame || shutdown_.load()) {
        return false;
    }

#ifdef FFMPEG_AVAILABLE
    FrameSpec spec(frame->width, frame->height, frame->format, config_.default_alignment);
    size_t frame_size = calculateFrameSize(spec);

    // 尝试归还到池中
    {
        std::lock_guard<std::mutex> lock(pools_mutex_);
        auto it = pools_.find(spec);
        if (it != pools_.end() && it->second) {
            if (it->second->release(frame)) {
                updateStatistics(true, frame_size, false);
                return true;
            }
        }
    }

    // 无法归还到池中，直接释放
    av_frame_free(&frame);
    updateStatistics(false, frame_size, false);
#endif

    return false;
}

void FrameAllocator::preallocateFrames(const FrameSpec& spec, size_t count) {
    auto pool = getOrCreatePool(spec);
    if (!pool) return;

    for (size_t i = 0; i < count; ++i) {
        AVFrame* frame = pool->createFrame();
        if (frame) {
            pool->release(frame);
        }
    }
}

std::shared_ptr<FrameAllocator::FramePool> FrameAllocator::getOrCreatePool(const FrameSpec& spec) {
    std::lock_guard<std::mutex> lock(pools_mutex_);

    auto it = pools_.find(spec);
    if (it != pools_.end()) {
        return it->second;
    }

    // 检查池数量限制
    if (pools_.size() >= config_.max_pools) {
        return nullptr;
    }

    // 创建新池
    auto pool = std::make_shared<FramePool>(spec, config_.frames_per_pool);
    pools_[spec] = pool;
    stats_.active_pools.fetch_add(1);

    return pool;
}

size_t FrameAllocator::calculateFrameSize(const FrameSpec& spec) const {
#ifdef FFMPEG_AVAILABLE
    int buffer_size = av_image_get_buffer_size(
        static_cast<AVPixelFormat>(spec.pixel_format),
        spec.width, spec.height, spec.alignment);

    return buffer_size > 0 ? static_cast<size_t>(buffer_size) : 0;
#else
    // 简单估算：YUV420P格式
    return spec.width * spec.height * 3 / 2;
#endif
}

void FrameAllocator::checkMemoryPressure() {
    size_t current = stats_.total_memory_usage.load();
    size_t peak = stats_.peak_memory_usage.load();

    if (current > peak) {
        stats_.peak_memory_usage.store(current);
        peak = current;
    }

    if (memory_pressure_callback_ && current > peak * 0.8) {
        memory_pressure_callback_(current, peak);
    }
}

void FrameAllocator::updateStatistics(bool from_pool, size_t frame_size, bool is_allocation) {
    if (!config_.enable_statistics) return;

    if (is_allocation) {
        stats_.total_allocated.fetch_add(1);
        stats_.total_memory_usage.fetch_add(frame_size);
    } else {
        stats_.total_freed.fetch_add(1);
        if (stats_.total_memory_usage.load() >= frame_size) {
            stats_.total_memory_usage.fetch_sub(frame_size);
        }
    }

    if (is_allocation) {
        checkMemoryPressure();
    }
}

std::vector<std::pair<FrameAllocator::FrameSpec, size_t>> FrameAllocator::getPoolInfo() const {
    std::lock_guard<std::mutex> lock(pools_mutex_);

    std::vector<std::pair<FrameSpec, size_t>> info;
    info.reserve(pools_.size());

    for (const auto& pair : pools_) {
        info.emplace_back(pair.first, pair.second->available());
    }

    return info;
}

void FrameAllocator::cleanup() {
    std::lock_guard<std::mutex> lock(pools_mutex_);

    auto it = pools_.begin();
    while (it != pools_.end()) {
        if (it->second->available() == it->second->capacity()) {
            // 池中所有帧都空闲，可以清理
            stats_.active_pools.fetch_sub(1);
            it = pools_.erase(it);
        } else {
            ++it;
        }
    }
}

void FrameAllocator::setMemoryPressureCallback(std::function<void(size_t, size_t)> callback) {
    memory_pressure_callback_ = std::move(callback);
}

void FrameAllocator::forceGarbageCollection() {
    std::lock_guard<std::mutex> lock(pools_mutex_);

    for (auto& pair : pools_) {
        // 这里可以实现更激进的清理策略
        // 例如清理部分空闲帧
    }
}

std::vector<FrameAllocator::FrameSpec> FrameAllocator::getRecommendedSpecs() const {
    std::lock_guard<std::mutex> lock(pools_mutex_);

    std::vector<FrameSpec> recommended;
    recommended.reserve(pools_.size());

    for (const auto& pair : pools_) {
        if (pair.second->getTotalAllocated() > 0) {
            recommended.push_back(pair.first);
        }
    }

    return recommended;
}

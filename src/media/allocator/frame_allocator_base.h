// frame_allocator.h - 抽象接口定义
#ifndef FRAME_ALLOCATOR_H
#define FRAME_ALLOCATOR_H

#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace media {

/**
 * @brief 通用帧数据结构
 * 
 * 这个结构体封装了不同后端的帧数据，提供统一的访问接口
 */
struct FrameData {
    void* data[4] = {nullptr};      // 平面数据指针 (Y, U, V, A)
    int linesize[4] = {0};          // 每个平面的行字节数
    int width = 0;                  // 帧宽度
    int height = 0;                 // 帧高度
    int format = 0;                 // 像素格式 (后端特定)
    size_t buffer_size = 0;         // 总缓冲区大小
    void* native_frame = nullptr;   // 后端特定的原生帧对象指针
    
    // 便利方法
    bool isValid() const { return data[0] != nullptr && width > 0 && height > 0; }
    size_t getTotalSize() const { return buffer_size; }
};

/**
 * @brief 帧规格定义
 * 
 * 定义了帧的基本属性，用作内存池的key
 */
struct FrameSpec {
    int width;                      // 帧宽度
    int height;                     // 帧高度  
    int pixel_format;               // 像素格式 (后端特定枚举值)
    int alignment;                  // 内存对齐要求

    FrameSpec(int w = 0, int h = 0, int fmt = 0, int align = 32)
        : width(w), height(h), pixel_format(fmt), alignment(align) {}

    bool operator==(const FrameSpec& other) const {
        return width == other.width && 
               height == other.height && 
               pixel_format == other.pixel_format && 
               alignment == other.alignment;
    }

    bool operator!=(const FrameSpec& other) const {
        return !(*this == other);
    }
};

/**
 * @brief FrameSpec的哈希函数，用于unordered_map
 */
struct FrameSpecHash {
    size_t operator()(const FrameSpec& spec) const {
        size_t h1 = std::hash<int>{}(spec.width);
        size_t h2 = std::hash<int>{}(spec.height);
        size_t h3 = std::hash<int>{}(spec.pixel_format);
        size_t h4 = std::hash<int>{}(spec.alignment);
        return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3);
    }
};

/**
 * @brief 帧分配结果
 */
struct AllocatedFrame {
    std::unique_ptr<FrameData> frame;   // 分配的帧数据
    bool from_pool = false;             // 是否来自对象池
    FrameSpec spec;                     // 帧规格
    std::string backend;                // 后端名称

    // 便利方法
    bool isValid() const { 
        return frame && frame->isValid(); 
    }
    
    // 获取原生帧对象 (需要转换类型)
    template<typename T>
    T* getNativeFrame() const {
        return frame ? static_cast<T*>(frame->native_frame) : nullptr;
    }
};

/**
 * @brief 分配器统计信息
 */
struct Statistics {
    size_t total_allocated = 0;        // 总分配次数
    size_t total_freed = 0;            // 总释放次数
    size_t pool_hits = 0;              // 池命中次数
    size_t pool_misses = 0;            // 池未命中次数
    size_t active_pools = 0;           // 活跃池数量
    size_t total_memory_usage = 0;     // 当前内存使用量 (bytes)
    size_t peak_memory_usage = 0;      // 峰值内存使用量 (bytes)
    std::string backend;               // 后端名称

    // 计算命中率
    double getHitRate() const {
        size_t total = pool_hits + pool_misses;
        return total > 0 ? static_cast<double>(pool_hits) / total : 0.0;
    }

    // 计算内存效率 (当前使用 / 峰值使用)
    double getMemoryEfficiency() const {
        return peak_memory_usage > 0 ? 
            static_cast<double>(total_memory_usage) / peak_memory_usage : 0.0;
    }

    // 计算平均帧大小
    double getAverageFrameSize() const {
        return total_allocated > 0 ? 
            static_cast<double>(total_memory_usage) / total_allocated : 0.0;
    }
};

/**
 * @brief 分配器配置基类
 */
struct AllocatorConfig {
    size_t max_pools = 32;             // 最大池数量
    size_t frames_per_pool = 16;       // 每个池的帧数量
    size_t max_frame_size = 64 * 1024 * 1024;  // 最大单帧大小 (64MB)
    int default_alignment = 32;        // 默认内存对齐 (AVX)
    bool enable_statistics = true;     // 是否启用统计
    bool enable_preallocation = true;  // 是否启用预分配

    virtual ~AllocatorConfig() = default;
};

/**
 * @brief 抽象帧分配器接口
 * 
 * 定义了所有后端必须实现的接口
 */
class IFrameAllocator {
public:
    virtual ~IFrameAllocator() = default;

    /**
     * @brief 分配视频帧
     * @param spec 帧规格
     * @return 分配结果，包含帧数据或空指针
     */
    virtual AllocatedFrame allocateFrame(const FrameSpec& spec) = 0;

    /**
     * @brief 释放视频帧
     * @param frame 要释放的帧数据
     * @return 是否成功释放到池中
     */
    virtual bool deallocateFrame(std::unique_ptr<FrameData> frame) = 0;

    /**
     * @brief 预分配指定规格的帧池
     * @param spec 帧规格
     * @param count 预分配数量
     */
    virtual void preallocateFrames(const FrameSpec& spec, size_t count) = 0;

    /**
     * @brief 获取统计信息
     * @return 当前的统计数据快照
     */
    virtual Statistics getStatistics() const = 0;

    /**
     * @brief 获取后端名称
     * @return 后端标识字符串 (如 "FFmpeg", "GStreamer")
     */
    virtual std::string getBackendName() const = 0;

    /**
     * @brief 获取池信息
     * @return 每个池的规格和可用帧数
     */
    virtual std::vector<std::pair<FrameSpec, size_t>> getPoolInfo() const = 0;

    /**
     * @brief 清理空闲的池
     * 移除长时间未使用的帧池以释放内存
     */
    virtual void cleanup() = 0;

    /**
     * @brief 设置内存压力回调
     * @param callback 内存使用过高时的回调函数
     */
    virtual void setMemoryPressureCallback(
        std::function<void(size_t current, size_t peak)> callback) = 0;

    /**
     * @brief 获取支持的像素格式列表
     * @return 后端支持的像素格式列表
     */
    virtual std::vector<int> getSupportedFormats() const = 0;

    /**
     * @brief 检查是否支持指定的像素格式
     * @param format 像素格式
     * @return 是否支持
     */
    virtual bool isFormatSupported(int format) const = 0;

    /**
     * @brief 计算指定规格的帧大小
     * @param spec 帧规格
     * @return 预估的帧大小 (bytes)
     */
    virtual size_t calculateFrameSize(const FrameSpec& spec) const = 0;

    /**
     * @brief 强制垃圾回收
     * 立即清理所有空闲帧和未使用的池
     */
    virtual void forceGarbageCollection() = 0;

    /**
     * @brief 获取推荐的帧规格
     * 基于历史使用模式推荐最优配置
     * @return 推荐的帧规格列表
     */
    virtual std::vector<FrameSpec> getRecommendedSpecs() const = 0;
};

/**
 * @brief 分配器错误类型
 */
enum class AllocatorError {
    Success,                    // 成功
    InvalidParameters,          // 参数无效
    UnsupportedFormat,         // 不支持的格式
    SizeLimit,                 // 超出大小限制
    OutOfMemory,               // 内存不足
    PoolFull,                  // 池已满
    BackendError,              // 后端错误
    NotInitialized             // 未初始化
};

/**
 * @brief 分配器异常类
 */
class AllocatorException : public std::exception {
private:
    AllocatorError error_;
    std::string message_;

public:
    AllocatorException(AllocatorError error, const std::string& message)
        : error_(error), message_(message) {}

    const char* what() const noexcept override {
        return message_.c_str();
    }

    AllocatorError getError() const { return error_; }
};

} // namespace media

#endif // FRAME_ALLOCATOR_H
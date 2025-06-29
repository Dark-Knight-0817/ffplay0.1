// frame_allocator_factory.h - 无Mock版本的工厂类定义
#ifndef FRAME_ALLOCATOR_FACTORY_H
#define FRAME_ALLOCATOR_FACTORY_H

#include "frame_allocator_base.h"
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <map>
#include <functional>

namespace media {

/**
 * @brief 支持的后端类型
 */
enum class BackendType {
    Auto,               // 自动检测最佳后端
    FFmpeg,             // FFmpeg后端
    GStreamer,          // GStreamer后端
    DirectShow,         // DirectShow后端 (Windows)
    MediaFoundation     // Media Foundation后端 (Windows)
};

/**
 * @brief 后端信息
 */
struct BackendInfo {
    BackendType type;
    std::string name;
    std::string version;
    bool available;
    std::string description;
    std::vector<std::string> supported_features;

    BackendInfo(BackendType t, const std::string& n, bool avail = false)
        : type(t), name(n), available(avail) {}
};

/**
 * @brief 帧分配器工厂类
 * 
 * 负责创建和管理不同后端的帧分配器实例
 */
class FrameAllocatorFactory {
public:
    /**
     * @brief 创建指定类型的分配器
     * @param type 后端类型
     * @param config 配置参数 (可选)
     * @return 分配器实例，失败抛出异常
     */
    static std::unique_ptr<IFrameAllocator> create(
        BackendType type = BackendType::Auto,
        std::unique_ptr<AllocatorConfig> config = nullptr);

    /**
     * @brief 根据名称创建分配器
     * @param backend_name 后端名称 ("ffmpeg", "gstreamer", 等)
     * @param config 配置参数 (可选)
     * @return 分配器实例，失败抛出异常
     */
    static std::unique_ptr<IFrameAllocator> create(
        const std::string& backend_name,
        std::unique_ptr<AllocatorConfig> config = nullptr);

    /**
     * @brief 获取可用的后端列表
     * @return 可用后端的名称列表
     */
    static std::vector<std::string> getAvailableBackends();

    /**
     * @brief 获取所有后端信息
     * @return 包含所有后端信息的列表
     */
    static std::vector<BackendInfo> getAllBackendInfo();

    /**
     * @brief 检测最佳可用后端
     * @return 推荐的后端类型
     */
    static BackendType detectBestBackend();

    /**
     * @brief 检查指定后端是否可用
     * @param type 后端类型
     * @return 是否可用
     */
    static bool isBackendAvailable(BackendType type);

    /**
     * @brief 将后端类型转换为字符串
     * @param type 后端类型
     * @return 字符串表示
     */
    static std::string backendTypeToString(BackendType type);

    /**
     * @brief 将字符串转换为后端类型
     * @param name 后端名称
     * @return 后端类型，未找到返回Auto
     */
    static BackendType stringToBackendType(const std::string& name);

    /**
     * @brief 注册自定义后端创建函数
     * @param name 后端名称
     * @param creator 创建函数
     */
    static void registerBackend(
        const std::string& name,
        std::function<std::unique_ptr<IFrameAllocator>(std::unique_ptr<AllocatorConfig>)> creator);

private:
    // 具体后端的创建函数 - 使用前向声明，不包含具体实现头文件
    static std::unique_ptr<IFrameAllocator> createFFmpegAllocator(std::unique_ptr<AllocatorConfig> config);
    static std::unique_ptr<IFrameAllocator> createGStreamerAllocator(std::unique_ptr<AllocatorConfig> config);
    static std::unique_ptr<IFrameAllocator> createDirectShowAllocator(std::unique_ptr<AllocatorConfig> config);
    static std::unique_ptr<IFrameAllocator> createMediaFoundationAllocator(std::unique_ptr<AllocatorConfig> config);

    // 后端可用性检测
    static bool isFFmpegAvailable();
    static bool isGStreamerAvailable();
    static bool isDirectShowAvailable();
    static bool isMediaFoundationAvailable();

    // 获取后端版本信息
    static std::string getFFmpegVersion();
    static std::string getGStreamerVersion();
    static std::string getDirectShowVersion();
    static std::string getMediaFoundationVersion();

    // 自定义后端注册表
    static std::mutex custom_backends_mutex_;
    static std::map<std::string, std::function<std::unique_ptr<IFrameAllocator>(std::unique_ptr<AllocatorConfig>)>> custom_backends_;
};

/**
 * @brief 全局帧分配器管理器
 * 
 * 提供全局单例访问，支持运行时切换后端
 */
class GlobalFrameAllocator {
private:
    static std::unique_ptr<IFrameAllocator> instance_;
    static BackendType current_backend_;
    static std::mutex instance_mutex_;
    static bool initialized_;

public:
    /**
     * @brief 初始化全局分配器
     * @param backend 后端类型
     * @param config 配置参数 (可选)
     * @throws AllocatorException 初始化失败时抛出
     */
    static void initialize(
        BackendType backend = BackendType::Auto,
        std::unique_ptr<AllocatorConfig> config = nullptr);

    /**
     * @brief 获取全局分配器实例
     * @return 分配器引用
     * @throws AllocatorException 未初始化时抛出
     */
    static IFrameAllocator& getInstance();

    /**
     * @brief 切换后端
     * @param backend 新的后端类型
     * @param config 新的配置参数 (可选)
     * @throws AllocatorException 切换失败时抛出
     */
    static void switchBackend(
        BackendType backend,
        std::unique_ptr<AllocatorConfig> config = nullptr);

    /**
     * @brief 关闭全局分配器
     */
    static void shutdown();

    /**
     * @brief 获取当前后端类型
     * @return 当前使用的后端类型
     */
    static BackendType getCurrentBackendType();

    /**
     * @brief 获取当前后端名称
     * @return 当前后端的名称字符串
     */
    static std::string getCurrentBackendName();

    /**
     * @brief 检查是否已初始化
     * @return 是否已初始化
     */
    static bool isInitialized();

    /**
     * @brief 获取全局统计信息
     * @return 统计信息，未初始化时返回空统计
     */
    static Statistics getGlobalStatistics();

private:
    GlobalFrameAllocator() = default;
    static void ensureInitialized();
};

/**
 * @brief 便捷的帧分配宏定义
 * 
 * 提供简化的全局访问接口
 */

// 确保全局分配器已初始化
#define ENSURE_FRAME_ALLOCATOR() \
    do { \
        if (!media::GlobalFrameAllocator::isInitialized()) { \
            media::GlobalFrameAllocator::initialize(); \
        } \
    } while(0)

// 分配帧
#define ALLOCATE_FRAME(width, height, format) \
    ([&]() { \
        ENSURE_FRAME_ALLOCATOR(); \
        media::FrameSpec spec(width, height, format); \
        return media::GlobalFrameAllocator::getInstance().allocateFrame(spec); \
    })()

// 分配帧 (带对齐)
#define ALLOCATE_FRAME_ALIGNED(width, height, format, alignment) \
    ([&]() { \
        ENSURE_FRAME_ALLOCATOR(); \
        media::FrameSpec spec(width, height, format, alignment); \
        return media::GlobalFrameAllocator::getInstance().allocateFrame(spec); \
    })()

// 释放帧
#define DEALLOCATE_FRAME(frame) \
    do { \
        if (frame && media::GlobalFrameAllocator::isInitialized()) { \
            media::GlobalFrameAllocator::getInstance().deallocateFrame(std::move(frame)); \
        } \
    } while(0)

// 获取分配器统计信息
#define GET_ALLOCATOR_STATS() \
    (media::GlobalFrameAllocator::isInitialized() ? \
     media::GlobalFrameAllocator::getGlobalStatistics() : \
     media::Statistics{})

// 切换后端
#define SWITCH_ALLOCATOR_BACKEND(backend) \
    media::GlobalFrameAllocator::switchBackend(media::BackendType::backend)

} // namespace media

#endif // FRAME_ALLOCATOR_FACTORY_H
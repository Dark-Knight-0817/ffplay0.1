// frame_allocator_factory.cpp - 无Mock版本的工厂实现
#include "frame_allocator_factory.h"
#include "ffmpeg_allocator/ffmpeg_frame_allocator.h" 
#include <algorithm>
#include <cctype>

namespace media {

// 静态成员定义
std::mutex FrameAllocatorFactory::custom_backends_mutex_;
std::map<std::string, std::function<std::unique_ptr<IFrameAllocator>(std::unique_ptr<AllocatorConfig>)>> 
    FrameAllocatorFactory::custom_backends_;

// GlobalFrameAllocator静态成员
std::unique_ptr<IFrameAllocator> GlobalFrameAllocator::instance_;
BackendType GlobalFrameAllocator::current_backend_ = BackendType::Auto;
std::mutex GlobalFrameAllocator::instance_mutex_;
bool GlobalFrameAllocator::initialized_ = false;

// FrameAllocatorFactory实现
std::unique_ptr<IFrameAllocator> FrameAllocatorFactory::create(
    BackendType type, 
    std::unique_ptr<AllocatorConfig> config) {
    
    // 如果是Auto，检测最佳后端
    if (type == BackendType::Auto) {
        type = detectBestBackend();
    }

    switch (type) {
        case BackendType::FFmpeg:
            return createFFmpegAllocator(std::move(config));
            
        case BackendType::GStreamer:
            return createGStreamerAllocator(std::move(config));
            
        case BackendType::DirectShow:
            return createDirectShowAllocator(std::move(config));
            
        case BackendType::MediaFoundation:
            return createMediaFoundationAllocator(std::move(config));
            
        default:
            throw AllocatorException(AllocatorError::InvalidParameters, 
                "Unsupported backend type");
    }
}

std::unique_ptr<IFrameAllocator> FrameAllocatorFactory::create(
    const std::string& backend_name, 
    std::unique_ptr<AllocatorConfig> config) {
    
    BackendType type = stringToBackendType(backend_name);
    
    // 检查自定义后端
    if (type == BackendType::Auto) {
        std::lock_guard<std::mutex> lock(custom_backends_mutex_);
        auto it = custom_backends_.find(backend_name);
        if (it != custom_backends_.end()) {
            return it->second(std::move(config));
        }
        
        throw AllocatorException(AllocatorError::InvalidParameters, 
            "Unknown backend: " + backend_name);
    }
    
    return create(type, std::move(config));
}

std::vector<std::string> FrameAllocatorFactory::getAvailableBackends() {
    std::vector<std::string> backends;
    
    if (isFFmpegAvailable()) {
        backends.push_back("ffmpeg");
    }
    if (isGStreamerAvailable()) {
        backends.push_back("gstreamer");
    }
    if (isDirectShowAvailable()) {
        backends.push_back("directshow");
    }
    if (isMediaFoundationAvailable()) {
        backends.push_back("mediafoundation");
    }
    
    // 如果没有任何后端可用，抛出异常
    if (backends.empty()) {
        throw AllocatorException(AllocatorError::BackendError, 
            "No multimedia backend available. Please install FFmpeg, GStreamer, or other supported libraries.");
    }
    
    // 添加自定义后端
    {
        std::lock_guard<std::mutex> lock(custom_backends_mutex_);
        for (const auto& pair : custom_backends_) {
            backends.push_back(pair.first);
        }
    }
    
    return backends;
}

std::vector<BackendInfo> FrameAllocatorFactory::getAllBackendInfo() {
    std::vector<BackendInfo> info;
    
    // FFmpeg
    {
        BackendInfo ffmpeg_info(BackendType::FFmpeg, "FFmpeg", isFFmpegAvailable());
        if (ffmpeg_info.available) {
            ffmpeg_info.version = getFFmpegVersion();
            ffmpeg_info.description = "Industry-standard multimedia framework";
            ffmpeg_info.supported_features = {"Hardware decoding", "Multiple formats", "High performance"};
        }
        info.push_back(ffmpeg_info);
    }
    
    // GStreamer
    {
        BackendInfo gstreamer_info(BackendType::GStreamer, "GStreamer", isGStreamerAvailable());
        if (gstreamer_info.available) {
            gstreamer_info.version = getGStreamerVersion();
            gstreamer_info.description = "Open source multimedia framework";
            gstreamer_info.supported_features = {"Pipeline-based", "Plugin architecture", "Cross-platform"};
        }
        info.push_back(gstreamer_info);
    }
    
    // DirectShow
    {
        BackendInfo ds_info(BackendType::DirectShow, "DirectShow", isDirectShowAvailable());
        if (ds_info.available) {
            ds_info.version = getDirectShowVersion();
            ds_info.description = "Microsoft multimedia framework";
            ds_info.supported_features = {"Windows native", "DirectX integration"};
        }
        info.push_back(ds_info);
    }
    
    // Media Foundation
    {
        BackendInfo mf_info(BackendType::MediaFoundation, "MediaFoundation", isMediaFoundationAvailable());
        if (mf_info.available) {
            mf_info.version = getMediaFoundationVersion();
            mf_info.description = "Modern Microsoft multimedia framework";
            mf_info.supported_features = {"Hardware acceleration", "Modern API", "Windows 7+"};
        }
        info.push_back(mf_info);
    }
    
    return info;
}

BackendType FrameAllocatorFactory::detectBestBackend() {
    // 优先级顺序：FFmpeg > GStreamer > MediaFoundation > DirectShow
    
    if (isFFmpegAvailable()) {
        return BackendType::FFmpeg;
    }
    
    if (isGStreamerAvailable()) {
        return BackendType::GStreamer;
    }
    
#ifdef _WIN32
    if (isMediaFoundationAvailable()) {
        return BackendType::MediaFoundation;
    }
    
    if (isDirectShowAvailable()) {
        return BackendType::DirectShow;
    }
#endif
    
    // 没有可用后端，抛出异常
    throw AllocatorException(AllocatorError::BackendError, 
        "No multimedia backend available. Please install FFmpeg, GStreamer, or other supported libraries.");
}

bool FrameAllocatorFactory::isBackendAvailable(BackendType type) {
    switch (type) {
        case BackendType::FFmpeg:
            return isFFmpegAvailable();
        case BackendType::GStreamer:
            return isGStreamerAvailable();
        case BackendType::DirectShow:
            return isDirectShowAvailable();
        case BackendType::MediaFoundation:
            return isMediaFoundationAvailable();
        default:
            return false;
    }
}

std::string FrameAllocatorFactory::backendTypeToString(BackendType type) {
    switch (type) {
        case BackendType::Auto: return "auto";
        case BackendType::FFmpeg: return "ffmpeg";
        case BackendType::GStreamer: return "gstreamer";
        case BackendType::DirectShow: return "directshow";
        case BackendType::MediaFoundation: return "mediafoundation";
        default: return "unknown";
    }
}

BackendType FrameAllocatorFactory::stringToBackendType(const std::string& name) {
    std::string lower_name = name;
    std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
    
    if (lower_name == "auto") return BackendType::Auto;
    if (lower_name == "ffmpeg") return BackendType::FFmpeg;
    if (lower_name == "gstreamer") return BackendType::GStreamer;
    if (lower_name == "directshow") return BackendType::DirectShow;
    if (lower_name == "mediafoundation") return BackendType::MediaFoundation;
    
    return BackendType::Auto;  // 未知类型返回Auto
}

void FrameAllocatorFactory::registerBackend(
    const std::string& name,
    std::function<std::unique_ptr<IFrameAllocator>(std::unique_ptr<AllocatorConfig>)> creator) {
    
    std::lock_guard<std::mutex> lock(custom_backends_mutex_);
    custom_backends_[name] = creator;
}

// 私有方法实现 - 后端创建函数
std::unique_ptr<IFrameAllocator> FrameAllocatorFactory::createFFmpegAllocator(
    std::unique_ptr<AllocatorConfig> config) {
    
    if (!isFFmpegAvailable()) {
        throw AllocatorException(AllocatorError::BackendError, 
            "FFmpeg backend is not available");
    }
    
    // 动态加载FFmpeg实现
    extern std::unique_ptr<IFrameAllocator> createFFmpegFrameAllocator(std::unique_ptr<AllocatorConfig> config);
    return createFFmpegFrameAllocator(std::move(config));
}

std::unique_ptr<IFrameAllocator> FrameAllocatorFactory::createGStreamerAllocator(
    std::unique_ptr<AllocatorConfig> config) {
    
    if (!isGStreamerAvailable()) {
        throw AllocatorException(AllocatorError::BackendError, 
            "GStreamer backend is not available");
    }
    
    // 预留给未来实现
    throw AllocatorException(AllocatorError::NotInitialized, 
        "GStreamer allocator not implemented yet");
}

std::unique_ptr<IFrameAllocator> FrameAllocatorFactory::createDirectShowAllocator(
    std::unique_ptr<AllocatorConfig> config) {
    
    if (!isDirectShowAvailable()) {
        throw AllocatorException(AllocatorError::BackendError, 
            "DirectShow backend is not available");
    }
    
    // 预留给未来实现
    throw AllocatorException(AllocatorError::NotInitialized, 
        "DirectShow allocator not implemented yet");
}

std::unique_ptr<IFrameAllocator> FrameAllocatorFactory::createMediaFoundationAllocator(
    std::unique_ptr<AllocatorConfig> config) {
    
    if (!isMediaFoundationAvailable()) {
        throw AllocatorException(AllocatorError::BackendError, 
            "MediaFoundation backend is not available");
    }
    
    // 预留给未来实现
    throw AllocatorException(AllocatorError::NotInitialized, 
        "MediaFoundation allocator not implemented yet");
}

// 后端可用性检测
bool FrameAllocatorFactory::isFFmpegAvailable() {
#ifdef FFMPEG_AVAILABLE
    return true;
#else
    // 运行时检测
    #ifdef __has_include
        #if __has_include(<libavutil/frame.h>)
            return true;
        #endif
    #endif
    return false;
#endif
}

bool FrameAllocatorFactory::isGStreamerAvailable() {
#ifdef GSTREAMER_AVAILABLE
    return true;
#else
    // 运行时检测
    #ifdef __has_include
        #if __has_include(<gst/gst.h>)
            return true;
        #endif
    #endif
    return false;
#endif
}

bool FrameAllocatorFactory::isDirectShowAvailable() {
#ifdef _WIN32
    #ifdef DIRECTSHOW_AVAILABLE
        return true;
    #else
        // Windows上总是可用
        return true;
    #endif
#else
    return false;  // 非Windows平台不支持
#endif
}

bool FrameAllocatorFactory::isMediaFoundationAvailable() {
#ifdef _WIN32
    #ifdef MEDIAFOUNDATION_AVAILABLE
        return true;
    #else
        // 检测Windows版本 (Windows 7+)
        return true;  // 简化实现，假设现代Windows都支持
    #endif
#else
    return false;  // 非Windows平台不支持
#endif
}

// 获取版本信息
std::string FrameAllocatorFactory::getFFmpegVersion() {
#ifdef FFMPEG_AVAILABLE
    // 这里可以调用FFmpeg的版本函数
    return "4.0+";  // 占位符
#else
    return "N/A";
#endif
}

std::string FrameAllocatorFactory::getGStreamerVersion() {
#ifdef GSTREAMER_AVAILABLE
    return "1.0+";  // 占位符
#else
    return "N/A";
#endif
}

std::string FrameAllocatorFactory::getDirectShowVersion() {
#ifdef _WIN32
    return "Windows SDK";
#else
    return "N/A";
#endif
}

std::string FrameAllocatorFactory::getMediaFoundationVersion() {
#ifdef _WIN32
    return "Windows 7+";
#else
    return "N/A";
#endif
}

// GlobalFrameAllocator实现
void GlobalFrameAllocator::initialize(
    BackendType backend, 
    std::unique_ptr<AllocatorConfig> config) {
    
    std::lock_guard<std::mutex> lock(instance_mutex_);
    
    if (initialized_) {
        throw AllocatorException(AllocatorError::InvalidParameters, 
            "GlobalFrameAllocator already initialized");
    }
    
    try {
        instance_ = FrameAllocatorFactory::create(backend, std::move(config));
        current_backend_ = backend;
        initialized_ = true;
    } catch (const std::exception& e) {
        throw AllocatorException(AllocatorError::BackendError, 
            "Failed to initialize GlobalFrameAllocator: " + std::string(e.what()));
    }
}

IFrameAllocator& GlobalFrameAllocator::getInstance() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    
    if (!initialized_ || !instance_) {
        throw AllocatorException(AllocatorError::NotInitialized, 
            "GlobalFrameAllocator not initialized. Call initialize() first.");
    }
    
    return *instance_;
}

void GlobalFrameAllocator::switchBackend(
    BackendType backend, 
    std::unique_ptr<AllocatorConfig> config) {
    
    std::lock_guard<std::mutex> lock(instance_mutex_);
    
    if (!initialized_) {
        throw AllocatorException(AllocatorError::NotInitialized, 
            "GlobalFrameAllocator not initialized");
    }
    
    // 保存旧的实例以确保平滑切换
    auto old_instance = std::move(instance_);
    
    try {
        instance_ = FrameAllocatorFactory::create(backend, std::move(config));
        current_backend_ = backend;
        
        // 成功切换，可以清理旧实例
        // old_instance会自动析构
        
    } catch (const std::exception& e) {
        // 切换失败，恢复旧实例
        instance_ = std::move(old_instance);
        throw AllocatorException(AllocatorError::BackendError, 
            "Failed to switch backend: " + std::string(e.what()));
    }
}

void GlobalFrameAllocator::shutdown() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    
    instance_.reset();
    initialized_ = false;
    current_backend_ = BackendType::Auto;
}

BackendType GlobalFrameAllocator::getCurrentBackendType() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    return current_backend_;
}

std::string GlobalFrameAllocator::getCurrentBackendName() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    
    if (!initialized_ || !instance_) {
        return "None";
    }
    
    return instance_->getBackendName();
}

bool GlobalFrameAllocator::isInitialized() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    return initialized_ && instance_ != nullptr;
}

Statistics GlobalFrameAllocator::getGlobalStatistics() {
    std::lock_guard<std::mutex> lock(instance_mutex_);
    
    if (!initialized_ || !instance_) {
        return Statistics{};  // 返回空统计
    }
    
    return instance_->getStatistics();
}

void GlobalFrameAllocator::ensureInitialized() {
    if (!isInitialized()) {
        initialize();  // 使用默认参数自动初始化
    }
}

} // namespace media
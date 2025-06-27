#include "object_pool.h"
#include <cassert>

// 注意：由于ObjectPool是模板类，大部分实现已经在头文件中
// 这里主要提供一些非模板的辅助函数和特化实现

// 如果需要特定类型的特化，可以在这里实现
// 例如：AVFrame的特化实现

#ifdef FFMPEG_AVAILABLE
extern "C" {
#include <libavutil/frame.h>
}

// AVFrame的特化实现
template<>
std::unique_ptr<AVFrame> ObjectPool<AVFrame>::createObject() {
    auto frame = factory_();
    if (!frame) {
        frame = std::unique_ptr<AVFrame>(av_frame_alloc());
    }

    if (config_.enable_statistics) {
        stats_.total_created.fetch_add(1);
    }

    return frame;
}

template<>
void ObjectPool<AVFrame>::resetObject(AVFrame* obj) {
    if (obj) {
        // AVFrame特定的重置逻辑
        av_frame_unref(obj);

        // 重置基本属性
        obj->width = 0;
        obj->height = 0;
        obj->format = AV_PIX_FMT_NONE;
        obj->nb_samples = 0;
        obj->pts = AV_NOPTS_VALUE;
        obj->pkt_dts = AV_NOPTS_VALUE;
        obj->duration = 0;

        // 如果有自定义重置函数，也调用它
        if (reset_function_) {
            reset_function_(obj);
        }
    }
}

// AVPacket的特化实现
template<>
void ObjectPool<AVPacket>::resetObject(AVPacket* obj) {
    if (obj) {
        // AVPacket特定的重置逻辑
        av_packet_unref(obj);

        // 重置基本属性
        obj->pts = AV_NOPTS_VALUE;
        obj->dts = AV_NOPTS_VALUE;
        obj->duration = 0;
        obj->stream_index = -1;
        obj->flags = 0;

        // 如果有自定义重置函数，也调用它
        if (reset_function_) {
            reset_function_(obj);
        }
    }
}

#endif // FFMPEG_AVAILABLE

// 一些常用类型的显式实例化
// 这样可以减少编译时间并避免链接问题

// 基础类型的实例化
template class ObjectPool<int>;
template class ObjectPool<double>;
template class ObjectPool<std::string>;

// 常用容器类型
template class ObjectPool<std::vector<uint8_t>>;
template class ObjectPool<std::vector<int>>;

#ifdef FFMPEG_AVAILABLE
// FFmpeg类型的实例化
template class ObjectPool<AVFrame>;
template class ObjectPool<AVPacket>;
#endif

// 工厂函数的一些便捷实现
namespace ObjectPoolHelpers {

#ifdef FFMPEG_AVAILABLE
    // AVFrame的默认工厂函数
std::unique_ptr<AVFrame> createAVFrame() {
    AVFrame* frame = av_frame_alloc();
    return std::unique_ptr<AVFrame>(frame);
}

// AVPacket的默认工厂函数
std::unique_ptr<AVPacket> createAVPacket() {
    AVPacket* packet = av_packet_alloc();
    return std::unique_ptr<AVPacket>(packet);
}

// AVFrame的重置函数
void resetAVFrame(AVFrame* frame) {
    if (frame) {
        av_frame_unref(frame);
        frame->width = 0;
        frame->height = 0;
        frame->format = AV_PIX_FMT_NONE;
        frame->nb_samples = 0;
        frame->pts = AV_NOPTS_VALUE;
    }
}

// AVPacket的重置函数
void resetAVPacket(AVPacket* packet) {
    if (packet) {
        av_packet_unref(packet);
        packet->pts = AV_NOPTS_VALUE;
        packet->dts = AV_NOPTS_VALUE;
        packet->stream_index = -1;
    }
}

// 便捷的AVFrame对象池创建函数
std::unique_ptr<ObjectPool<AVFrame>> createAVFramePool(size_t initial_size = 16, size_t max_size = 128) {
    ObjectPool<AVFrame>::Config config;
    config.initial_size = initial_size;
    config.max_size = max_size;
    config.auto_expand = true;
    config.enable_statistics = true;

    auto pool = std::make_unique<ObjectPool<AVFrame>>(config);
    pool->setFactory(createAVFrame);
    pool->setResetFunction(resetAVFrame);

    return pool;
}

// 便捷的AVPacket对象池创建函数
std::unique_ptr<ObjectPool<AVPacket>> createAVPacketPool(size_t initial_size = 32, size_t max_size = 256) {
    ObjectPool<AVPacket>::Config config;
    config.initial_size = initial_size;
    config.max_size = max_size;
    config.auto_expand = true;
    config.enable_statistics = true;

    auto pool = std::make_unique<ObjectPool<AVPacket>>(config);
    pool->setFactory(createAVPacket);
    pool->setResetFunction(resetAVPacket);

    return pool;
}

#endif // FFMPEG_AVAILABLE

// 通用的vector<uint8_t>池（用于缓冲区）
std::unique_ptr<ObjectPool<std::vector<uint8_t>>> createBufferPool(
    size_t buffer_size = 1024, size_t initial_size = 16, size_t max_size = 128) {

    ObjectPool<std::vector<uint8_t>>::Config config;
    config.initial_size = initial_size;
    config.max_size = max_size;
    config.auto_expand = true;
    config.enable_statistics = true;

    auto pool = std::make_unique<ObjectPool<std::vector<uint8_t>>>(config);

    // 设置工厂函数，创建指定大小的缓冲区
    pool->setFactory([buffer_size]() {
        return std::make_unique<std::vector<uint8_t>>(buffer_size);
    });

    // 设置重置函数，清空缓冲区但保持容量
    pool->setResetFunction([](std::vector<uint8_t>* buffer) {
        if (buffer) {
            buffer->clear();
            // 可选：重置到初始大小
            // buffer->resize(initial_capacity);
        }
    });

    return pool;
}

} // namespace ObjectPoolHelpers

// 全局对象池管理器的实现
class GlobalObjectPoolManager {
private:
    std::mutex pools_mutex_;
    std::unordered_map<std::string, std::shared_ptr<void>> pools_;

public:
    static GlobalObjectPoolManager& getInstance() {
        static GlobalObjectPoolManager instance;
        return instance;
    }

    template<typename T>
    std::shared_ptr<ObjectPool<T>> getPool(const std::string& name) {
        std::lock_guard<std::mutex> lock(pools_mutex_);

        auto it = pools_.find(name);
        if (it != pools_.end()) {
            return std::static_pointer_cast<ObjectPool<T>>(it->second);
        }

        return nullptr;
    }

    template<typename T>
    void registerPool(const std::string& name, std::shared_ptr<ObjectPool<T>> pool) {
        std::lock_guard<std::mutex> lock(pools_mutex_);
        pools_[name] = std::static_pointer_cast<void>(pool);
    }

    void removePool(const std::string& name) {
        std::lock_guard<std::mutex> lock(pools_mutex_);
        pools_.erase(name);
    }

    std::vector<std::string> getPoolNames() const {
        std::lock_guard<std::mutex> lock(pools_mutex_);

        std::vector<std::string> names;
        names.reserve(pools_.size());

        for (const auto& pair : pools_) {
            names.push_back(pair.first);
        }

        return names;
    }

    void clearAllPools() {
        std::lock_guard<std::mutex> lock(pools_mutex_);
        pools_.clear();
    }
};

// 便捷的全局访问函数
namespace GlobalPools {

#ifdef FFMPEG_AVAILABLE
ObjectPool<AVFrame>& getAVFramePool() {
    static auto pool = ObjectPoolHelpers::createAVFramePool();
    return *pool;
}

ObjectPool<AVPacket>& getAVPacketPool() {
    static auto pool = ObjectPoolHelpers::createAVPacketPool();
    return *pool;
}
#endif

ObjectPool<std::vector<uint8_t>>& getBufferPool() {
    static auto pool = ObjectPoolHelpers::createBufferPool();
    return *pool;
}

template<typename T>
std::shared_ptr<ObjectPool<T>> getNamedPool(const std::string& name) {
    return GlobalObjectPoolManager::getInstance().getPool<T>(name);
}

template<typename T>
void registerNamedPool(const std::string& name, std::shared_ptr<ObjectPool<T>> pool) {
    GlobalObjectPoolManager::getInstance().registerPool(name, pool);
}

} // namespace GlobalPools

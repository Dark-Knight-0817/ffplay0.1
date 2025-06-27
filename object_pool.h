#ifndef OBJECT_POOL_H
#define OBJECT_POOL_H

#include <vector>
#include <queue>
#include <mutex>
#include <memory>
#include <functional>
#include <atomic>
#include <condition_variable>

/**
 * @brief 高性能泛型对象池
 *
 * 设计特点：
 * 1. 类型安全：使用模板支持任意类型
 * 2. 线程安全：支持多线程并发获取/归还
 * 3. 自动扩展：池空时自动创建新对象
 * 4. 对象重置：归还时自动调用reset方法
 * 5. 性能监控：提供详细的使用统计
 * 6. 生命周期管理：RAII + 智能指针
 */

template<typename T>
class ObjectPool {
public:
    /**
     * @brief 对象池配置
     */
    struct Config {
        size_t initial_size;        // 初始对象数量
        size_t max_size;           // 最大对象数量
        bool auto_expand;          // 是否自动扩展
        bool enable_statistics;    // 是否启用统计

        Config()
            : initial_size(16)
            , max_size(128)
            , auto_expand(true)
            , enable_statistics(true)
        {}
    };

    /**
     * @brief 统计信息
     */
    struct Statistics {
        std::atomic<size_t> total_created{0};      // 总创建数量
        std::atomic<size_t> total_acquired{0};     // 总获取次数
        std::atomic<size_t> total_released{0};     // 总归还次数
        std::atomic<size_t> current_in_use{0};     // 当前使用中
        std::atomic<size_t> current_available{0};  // 当前可用数量
        std::atomic<size_t> peak_usage{0};         // 峰值使用量

        // 计算命中率
        double getHitRate() const {
            size_t acquired = total_acquired.load();
            size_t created = total_created.load();
            return acquired > 0 ? static_cast<double>(acquired - created) / acquired : 0.0;
        }
    };

    /**
     * @brief 池化对象的智能指针包装
     */
    class PooledObject {
    public:
        PooledObject(std::unique_ptr<T> obj, ObjectPool<T>* pool)
            : object_(std::move(obj)), pool_(pool) {}

        ~PooledObject() {
            if (object_ && pool_) {
                pool_->releaseObject(std::move(object_));
            }
        }

        // 禁用拷贝
        PooledObject(const PooledObject&) = delete;
        PooledObject& operator=(const PooledObject&) = delete;

        // 支持移动
        PooledObject(PooledObject&& other) noexcept
            : object_(std::move(other.object_)), pool_(other.pool_) {
            other.pool_ = nullptr;
        }

        PooledObject& operator=(PooledObject&& other) noexcept {
            if (this != &other) {
                if (object_ && pool_) {
                    pool_->releaseObject(std::move(object_));
                }
                object_ = std::move(other.object_);
                pool_ = other.pool_;
                other.pool_ = nullptr;
            }
            return *this;
        }

        T* get() const { return object_.get(); }
        T* operator->() const { return object_.get(); }
        T& operator*() const { return *object_.get(); }

        explicit operator bool() const { return object_ != nullptr; }

    private:
        std::unique_ptr<T> object_;
        ObjectPool<T>* pool_;
    };

    using PooledPtr = std::unique_ptr<PooledObject>;

public:
    /**
     * @brief 构造函数
     * @param config 对象池配置
     */
    explicit ObjectPool(const Config& config = Config{});

    /**
     * @brief 析构函数
     */
    ~ObjectPool();

    // 禁用拷贝和赋值
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    /**
     * @brief 从池中获取对象
     * @return 池化对象的智能指针
     */
    PooledPtr acquire();

    /**
     * @brief 获取统计信息
     */
    Statistics getStatistics() const { return stats_; }

    /**
     * @brief 获取当前可用对象数量
     */
    size_t available() const;

    /**
     * @brief 获取当前使用中对象数量
     */
    size_t inUse() const;

    /**
     * @brief 预热池（预创建对象）
     * @param count 要预创建的对象数量
     */
    void warmup(size_t count);

    /**
     * @brief 清空池中的空闲对象
     */
    void clear();

    /**
     * @brief 设置对象工厂函数
     * @param factory 创建对象的工厂函数
     */
    void setFactory(std::function<std::unique_ptr<T>()> factory);

    /**
     * @brief 设置对象重置函数
     * @param reset_func 重置对象状态的函数
     */
    void setResetFunction(std::function<void(T*)> reset_func);

private:
    /**
     * @brief 创建新对象
     */
    std::unique_ptr<T> createObject();

    /**
     * @brief 重置对象状态
     */
    void resetObject(T* obj);

    /**
     * @brief 归还对象到池中（内部使用）
     */
    void releaseObject(std::unique_ptr<T> obj);

    /**
     * @brief 扩展池容量
     */
    void expandPool();

private:
    Config config_;                                    // 配置信息
    mutable Statistics stats_;                         // 统计信息

    mutable std::mutex pool_mutex_;                    // 池访问锁
    std::queue<std::unique_ptr<T>> available_objects_; // 可用对象队列

    std::function<std::unique_ptr<T>()> factory_;      // 对象工厂函数
    std::function<void(T*)> reset_function_;           // 对象重置函数

    std::atomic<bool> shutdown_{false};                // 关闭标志
};

/**
 * @brief 对象池实现
 */
template<typename T>
ObjectPool<T>::ObjectPool(const Config& config) : config_(config) {
    // 设置默认工厂函数
    factory_ = []() { return std::make_unique<T>(); };

    // 设置默认重置函数
    reset_function_ = [](T* obj) {
        // 尝试调用对象的reset方法
        if constexpr (requires { obj->reset(); }) {
            obj->reset();
        }
    };

    // 预创建初始对象
    warmup(config_.initial_size);
}

template<typename T>
ObjectPool<T>::~ObjectPool() {
    shutdown_.store(true);
    clear();
}

template<typename T>
typename ObjectPool<T>::PooledPtr ObjectPool<T>::acquire() {
    if (shutdown_.load()) {
        return nullptr;
    }

    std::unique_ptr<T> obj;

    {
        std::lock_guard<std::mutex> lock(pool_mutex_);

        if (!available_objects_.empty()) {
            // 从池中获取现有对象
            obj = std::move(available_objects_.front());
            available_objects_.pop();
            stats_.current_available.fetch_sub(1);
        }
    }

    if (!obj) {
        // 池为空，创建新对象
        if (config_.auto_expand &&
            stats_.current_in_use.load() < config_.max_size) {
            obj = createObject();
        } else {
            return nullptr; // 达到最大限制
        }
    }

    // 更新统计信息
    if (config_.enable_statistics) {
        stats_.total_acquired.fetch_add(1);
        size_t current_usage = stats_.current_in_use.fetch_add(1) + 1;

        // 更新峰值使用量
        size_t old_peak = stats_.peak_usage.load();
        while (current_usage > old_peak &&
               !stats_.peak_usage.compare_exchange_weak(old_peak, current_usage)) {
            // 循环直到成功更新峰值
        }
    }

    return std::make_unique<PooledObject>(std::move(obj), this);
}

template<typename T>
size_t ObjectPool<T>::available() const {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    return available_objects_.size();
}

template<typename T>
size_t ObjectPool<T>::inUse() const {
    return stats_.current_in_use.load();
}

template<typename T>
void ObjectPool<T>::warmup(size_t count) {
    std::lock_guard<std::mutex> lock(pool_mutex_);

    for (size_t i = 0; i < count; ++i) {
        auto obj = createObject();
        if (obj) {
            available_objects_.push(std::move(obj));
            stats_.current_available.fetch_add(1);
        }
    }
}

template<typename T>
void ObjectPool<T>::clear() {
    std::lock_guard<std::mutex> lock(pool_mutex_);

    while (!available_objects_.empty()) {
        available_objects_.pop();
    }

    stats_.current_available.store(0);
}

template<typename T>
void ObjectPool<T>::setFactory(std::function<std::unique_ptr<T>()> factory) {
    factory_ = std::move(factory);
}

template<typename T>
void ObjectPool<T>::setResetFunction(std::function<void(T*)> reset_func) {
    reset_function_ = std::move(reset_func);
}

template<typename T>
std::unique_ptr<T> ObjectPool<T>::createObject() {
    auto obj = factory_();

    if (config_.enable_statistics) {
        stats_.total_created.fetch_add(1);
    }

    return obj;
}

template<typename T>
void ObjectPool<T>::resetObject(T* obj) {
    if (obj && reset_function_) {
        reset_function_(obj);
    }
}

template<typename T>
void ObjectPool<T>::releaseObject(std::unique_ptr<T> obj) {
    if (shutdown_.load() || !obj) {
        return;
    }

    // 重置对象状态
    resetObject(obj.get());

    {
        std::lock_guard<std::mutex> lock(pool_mutex_);

        // 检查池是否已满
        if (available_objects_.size() < config_.max_size) {
            available_objects_.push(std::move(obj));
            stats_.current_available.fetch_add(1);
        }
        // 如果池已满，对象会自动销毁
    }

    // 更新统计信息
    if (config_.enable_statistics) {
        stats_.total_released.fetch_add(1);
        stats_.current_in_use.fetch_sub(1);
    }
}

template<typename T>
void ObjectPool<T>::expandPool() {
    std::lock_guard<std::mutex> lock(pool_mutex_);

    size_t expand_count = std::min(config_.initial_size,
                                   config_.max_size - available_objects_.size());

    for (size_t i = 0; i < expand_count; ++i) {
        auto obj = createObject();
        if (obj) {
            available_objects_.push(std::move(obj));
            stats_.current_available.fetch_add(1);
        }
    }
}

#endif // OBJECT_POOL_H

#ifndef CACHE_MANAGER_H
#define CACHE_MANAGER_H

#include <memory>
#include <unordered_map>
#include <list>
#include <mutex>
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include <condition_variable>
#include <string>

/**
 * @brief 智能多级缓存管理器
 *
 * 设计特点：
 * 1. 多级缓存：L1(热数据)、L2(温数据)、L3(冷数据)
 * 2. 缓存策略：支持LRU、LFU、时间衰减等策略
 * 3. 预取机制：智能预测和预加载数据
 * 4. 压缩存储：对冷数据进行压缩以节省内存
 * 5. 命中率优化：动态调整缓存策略以提升命中率
 * 6. 线程安全：支持高并发访问
 */
template<typename Key, typename Value>
class CacheManager {
public:
    /**
     * @brief 缓存策略枚举
     */
    enum class EvictionPolicy {
        LRU,           // 最近最少使用
        LFU,           // 最不经常使用
        FIFO,          // 先进先出
        RANDOM,        // 随机淘汰
        TTL,           // 基于时间生存期
        ADAPTIVE       // 自适应策略
    };

    /**
     * @brief 缓存级别
     */
    enum class CacheLevel {
        L1 = 1,        // 一级缓存：最热数据
        L2 = 2,        // 二级缓存：温数据
        L3 = 3         // 三级缓存：冷数据
    };

    /**
     * @brief 缓存配置
     */
    struct Config {
        size_t l1_capacity;              // L1缓存容量
        size_t l2_capacity;              // L2缓存容量
        size_t l3_capacity;              // L3缓存容量
        EvictionPolicy l1_policy;        // L1淘汰策略
        EvictionPolicy l2_policy;        // L2淘汰策略
        EvictionPolicy l3_policy;        // L3淘汰策略
        bool enable_compression;         // 启用压缩
        bool enable_prefetch;            // 启用预取
        bool enable_statistics;          // 启用统计
        size_t ttl_seconds;              // TTL策略的生存时间(秒)
        double promote_threshold;        // 提升到上级缓存的阈值
        double demote_threshold;         // 降级到下级缓存的阈值
        size_t cleanup_interval_ms;      // 清理间隔(毫秒)

        Config()
            : l1_capacity(1000)
            , l2_capacity(5000)
            , l3_capacity(20000)
            , l1_policy(EvictionPolicy::LRU)
            , l2_policy(EvictionPolicy::LRU)
            , l3_policy(EvictionPolicy::LFU)
            , enable_compression(true)
            , enable_prefetch(true)
            , enable_statistics(true)
            , ttl_seconds(3600)  // 1小时
            , promote_threshold(0.8)
            , demote_threshold(0.2)
            , cleanup_interval_ms(60000)  // 1分钟
        {}
    };

    /**
     * @brief 缓存项元数据
     */
    struct CacheEntry {
        Value value;
        std::chrono::steady_clock::time_point created_time;
        std::chrono::steady_clock::time_point last_access_time;
        std::atomic<size_t> access_count{0};
        std::atomic<size_t> hit_count{0};
        size_t size;                    // 数据大小
        bool is_compressed;             // 是否已压缩
        CacheLevel level;               // 当前缓存级别

        CacheEntry(Value&& val, size_t sz, CacheLevel lvl)
            : value(std::move(val))
            , created_time(std::chrono::steady_clock::now())
            , last_access_time(created_time)
            , size(sz)
            , is_compressed(false)
            , level(lvl)
        {}
    };

    /**
     * @brief 缓存统计信息
     */
    struct Statistics {
        std::atomic<size_t> l1_hits{0};
        std::atomic<size_t> l2_hits{0};
        std::atomic<size_t> l3_hits{0};
        std::atomic<size_t> misses{0};
        std::atomic<size_t> evictions{0};
        std::atomic<size_t> promotions{0};
        std::atomic<size_t> demotions{0};
        std::atomic<size_t> compressions{0};
        std::atomic<size_t> prefetch_hits{0};
        std::atomic<size_t> prefetch_misses{0};

        // 计算总命中率
        double getTotalHitRate() const {
            size_t total_hits = l1_hits.load() + l2_hits.load() + l3_hits.load();
            size_t total = total_hits + misses.load();
            return total > 0 ? static_cast<double>(total_hits) / total : 0.0;
        }

        // 计算L1命中率
        double getL1HitRate() const {
            size_t total = l1_hits.load() + l2_hits.load() + l3_hits.load() + misses.load();
            return total > 0 ? static_cast<double>(l1_hits.load()) / total : 0.0;
        }

        // 计算预取效率
        double getPrefetchEfficiency() const {
            size_t total_prefetch = prefetch_hits.load() + prefetch_misses.load();
            return total_prefetch > 0 ? static_cast<double>(prefetch_hits.load()) / total_prefetch : 0.0;
        }
    };

private:
    /**
     * @brief 单级缓存实现
     */
    class SingleLevelCache {
    public:
        SingleLevelCache(size_t capacity, EvictionPolicy policy);
        ~SingleLevelCache() = default;

        // 基本操作
        std::shared_ptr<CacheEntry> get(const Key& key);
        bool put(const Key& key, std::shared_ptr<CacheEntry> entry);
        bool remove(const Key& key);
        void clear();

        // 容量管理
        size_t size() const;
        size_t capacity() const { return capacity_; }
        bool isFull() const { return size() >= capacity_; }

        // 淘汰操作
        std::vector<std::pair<Key, std::shared_ptr<CacheEntry>>> evictLeastUsed(size_t count);

        // 遍历操作
        std::vector<std::pair<Key, std::shared_ptr<CacheEntry>>> getAllEntries() const;

    private:
        size_t capacity_;
        EvictionPolicy policy_;

        mutable std::mutex mutex_;
        std::unordered_map<Key, std::shared_ptr<CacheEntry>> cache_map_;

        // LRU链表
        std::list<Key> lru_list_;
        std::unordered_map<Key, typename std::list<Key>::iterator> lru_map_;

        // LFU数据结构
        std::unordered_map<size_t, std::list<Key>> frequency_lists_;
        std::unordered_map<Key, std::pair<size_t, typename std::list<Key>::iterator>> frequency_map_;
        size_t min_frequency_;

        void updateLRU(const Key& key);
        void updateLFU(const Key& key);
        std::pair<Key, std::shared_ptr<CacheEntry>> evictOne();
    };

public:
    /**
     * @brief 构造函数
     * @param config 缓存配置
     */
    explicit CacheManager(const Config& config = Config{});

    /**
     * @brief 析构函数
     */
    ~CacheManager();

    // 禁用拷贝和赋值
    CacheManager(const CacheManager&) = delete;
    CacheManager& operator=(const CacheManager&) = delete;

    /**
     * @brief 获取缓存项
     * @param key 缓存键
     * @return 缓存值的智能指针，未找到则返回nullptr
     */
    std::shared_ptr<Value> get(const Key& key);

    /**
     * @brief 设置缓存项
     * @param key 缓存键
     * @param value 缓存值
     * @param size 数据大小（用于容量计算）
     * @param level 指定缓存级别（可选）
     * @return 是否成功设置
     */
    bool put(const Key& key, Value&& value, size_t size,
             CacheLevel level = CacheLevel::L1);

    /**
     * @brief 删除缓存项
     * @param key 缓存键
     * @return 是否成功删除
     */
    bool remove(const Key& key);

    /**
     * @brief 检查缓存项是否存在
     * @param key 缓存键
     * @return 是否存在
     */
    bool contains(const Key& key) const;

    /**
     * @brief 清空所有缓存
     */
    void clear();

    /**
     * @brief 获取统计信息
     */
    Statistics getStatistics() const { return stats_; }

    /**
     * @brief 获取缓存使用情况
     */
    std::tuple<size_t, size_t, size_t> getCacheSizes() const;

    /**
     * @brief 预取数据
     * @param keys 要预取的键列表
     * @param loader 数据加载函数
     */
    void prefetch(const std::vector<Key>& keys,
                  std::function<Value(const Key&)> loader);

    /**
     * @brief 设置数据压缩函数
     * @param compressor 压缩函数
     * @param decompressor 解压函数
     */
    void setCompressionFunctions(
        std::function<std::vector<uint8_t>(const Value&)> compressor,
        std::function<Value(const std::vector<uint8_t>&)> decompressor);

    /**
     * @brief 强制垃圾回收
     * 清理过期和最少使用的缓存项
     */
    void forceGarbageCollection();

    /**
     * @brief 优化缓存配置
     * 根据使用模式动态调整缓存策略
     */
    void optimizeConfiguration();

    /**
     * @brief 获取缓存报告
     */
    std::string generateReport() const;

    /**
     * @brief 设置缓存预警回调
     * 当缓存使用率过高时触发
     */
    void setCacheWarningCallback(std::function<void(CacheLevel, double)> callback);

private:
    /**
     * @brief 查找缓存项（内部方法）
     */
    std::pair<std::shared_ptr<CacheEntry>, CacheLevel> findEntry(const Key& key);

    /**
     * @brief 提升缓存项到上级
     */
    void promoteEntry(const Key& key, std::shared_ptr<CacheEntry> entry);

    /**
     * @brief 降级缓存项到下级
     */
    void demoteEntry(const Key& key, std::shared_ptr<CacheEntry> entry);

    /**
     * @brief 压缩缓存项
     */
    void compressEntry(std::shared_ptr<CacheEntry> entry);

    /**
     * @brief 解压缓存项
     */
    void decompressEntry(std::shared_ptr<CacheEntry> entry);

    /**
     * @brief 检查是否需要提升/降级
     */
    void checkForPromotion(std::shared_ptr<CacheEntry> entry);

    /**
     * @brief 清理过期项
     */
    void cleanupExpiredEntries();

    /**
     * @brief 后台维护线程
     */
    void maintenanceThread();

    /**
     * @brief 启动维护线程
     */
    void startMaintenanceThread();

    /**
     * @brief 停止维护线程
     */
    void stopMaintenanceThread();

    /**
     * @brief 更新统计信息
     */
    void updateStatistics(CacheLevel level, bool hit);

private:
    Config config_;                                    // 配置信息
    mutable Statistics stats_;                         // 统计信息

    // 三级缓存
    std::unique_ptr<SingleLevelCache> l1_cache_;
    std::unique_ptr<SingleLevelCache> l2_cache_;
    std::unique_ptr<SingleLevelCache> l3_cache_;

    // 压缩功能
    std::function<std::vector<uint8_t>(const Value&)> compressor_;
    std::function<Value(const std::vector<uint8_t>&)> decompressor_;

    // 预警回调
    std::function<void(CacheLevel, double)> warning_callback_;

    // 维护线程
    std::thread maintenance_thread_;
    std::atomic<bool> maintenance_running_{false};
    std::atomic<bool> shutdown_{false};
    std::condition_variable maintenance_cv_;
    std::mutex maintenance_mutex_;

    // 全局锁（用于级别间操作）
    mutable std::mutex global_mutex_;
};

/**
 * @brief 全局缓存管理器
 */
template<typename Key, typename Value>
class GlobalCacheManager {
public:
    static CacheManager<Key, Value>& getInstance() {
        static CacheManager<Key, Value> instance;
        return instance;
    }

    static void configure(const typename CacheManager<Key, Value>::Config& config) {
        getInstance() = CacheManager<Key, Value>(config);
    }

private:
    GlobalCacheManager() = default;
};

/**
 * @brief 便捷的缓存访问宏
 */
#define CACHE_GET(key) \
GlobalCacheManager<decltype(key), auto>::getInstance().get(key)

#define CACHE_PUT(key, value, size) \
    GlobalCacheManager<decltype(key), decltype(value)>::getInstance().put(key, std::move(value), size)

#endif // CACHE_MANAGER_H

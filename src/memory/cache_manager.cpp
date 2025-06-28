#include "cache_manager.h"
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>

// SingleLevelCache 实现
template<typename Key, typename Value>
CacheManager<Key, Value>::SingleLevelCache::SingleLevelCache(size_t capacity, EvictionPolicy policy)
    : capacity_(capacity), policy_(policy), min_frequency_(1) {
}

template<typename Key, typename Value>
std::shared_ptr<typename CacheManager<Key, Value>::CacheEntry>
CacheManager<Key, Value>::SingleLevelCache::get(const Key& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = cache_map_.find(key);
    if (it == cache_map_.end()) {
        return nullptr;
    }

    auto entry = it->second;
    entry->access_count.fetch_add(1);
    entry->hit_count.fetch_add(1);
    entry->last_access_time = std::chrono::steady_clock::now();

    // 更新访问顺序
    switch (policy_) {
    case EvictionPolicy::LRU:
        updateLRU(key);
        break;
    case EvictionPolicy::LFU:
        updateLFU(key);
        break;
    default:
        break;
    }

    return entry;
}

template<typename Key, typename Value>
bool CacheManager<Key, Value>::SingleLevelCache::put(const Key& key, std::shared_ptr<CacheEntry> entry) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 检查是否已存在
    auto it = cache_map_.find(key);
    if (it != cache_map_.end()) {
        // 更新现有条目
        it->second = entry;
        updateLRU(key);
        return true;
    }

    // 检查容量
    if (cache_map_.size() >= capacity_) {
        // 需要淘汰
        auto evicted = evictOne();
        if (evicted.first == key) {
            // 正好淘汰的是要插入的key，直接插入
            cache_map_[key] = entry;
            return true;
        }
    }

    // 插入新条目
    cache_map_[key] = entry;

    switch (policy_) {
    case EvictionPolicy::LRU:
        lru_list_.push_front(key);
        lru_map_[key] = lru_list_.begin();
        break;
    case EvictionPolicy::LFU:
        frequency_lists_[1].push_front(key);
        frequency_map_[key] = {1, frequency_lists_[1].begin()};
        min_frequency_ = 1;
        break;
    default:
        break;
    }

    return true;
}

template<typename Key, typename Value>
bool CacheManager<Key, Value>::SingleLevelCache::remove(const Key& key) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = cache_map_.find(key);
    if (it == cache_map_.end()) {
        return false;
    }

    // 从数据结构中移除
    switch (policy_) {
    case EvictionPolicy::LRU: {
        auto lru_it = lru_map_.find(key);
        if (lru_it != lru_map_.end()) {
            lru_list_.erase(lru_it->second);
            lru_map_.erase(lru_it);
        }
        break;
    }
    case EvictionPolicy::LFU: {
        auto freq_it = frequency_map_.find(key);
        if (freq_it != frequency_map_.end()) {
            size_t freq = freq_it->second.first;
            auto list_it = freq_it->second.second;
            frequency_lists_[freq].erase(list_it);
            frequency_map_.erase(freq_it);

            // 更新最小频率
            if (frequency_lists_[min_frequency_].empty()) {
                min_frequency_++;
            }
        }
        break;
    }
    default:
        break;
    }

    cache_map_.erase(it);
    return true;
}

template<typename Key, typename Value>
void CacheManager<Key, Value>::SingleLevelCache::clear() {
    std::lock_guard<std::mutex> lock(mutex_);

    cache_map_.clear();
    lru_list_.clear();
    lru_map_.clear();
    frequency_lists_.clear();
    frequency_map_.clear();
    min_frequency_ = 1;
}

template<typename Key, typename Value>
size_t CacheManager<Key, Value>::SingleLevelCache::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_map_.size();
}

template<typename Key, typename Value>
std::vector<std::pair<Key, std::shared_ptr<typename CacheManager<Key, Value>::CacheEntry>>>
CacheManager<Key, Value>::SingleLevelCache::evictLeastUsed(size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::pair<Key, std::shared_ptr<CacheEntry>>> evicted;
    evicted.reserve(count);

    for (size_t i = 0; i < count && !cache_map_.empty(); ++i) {
        auto evicted_pair = evictOne();
        if (evicted_pair.second != nullptr) {
            evicted.push_back(evicted_pair);
        }
    }

    return evicted;
}

template<typename Key, typename Value>
std::vector<std::pair<Key, std::shared_ptr<typename CacheManager<Key, Value>::CacheEntry>>>
CacheManager<Key, Value>::SingleLevelCache::getAllEntries() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::pair<Key, std::shared_ptr<CacheEntry>>> entries;
    entries.reserve(cache_map_.size());

    for (const auto& pair : cache_map_) {
        entries.push_back(pair);
    }

    return entries;
}

template<typename Key, typename Value>
void CacheManager<Key, Value>::SingleLevelCache::updateLRU(const Key& key) {
    auto lru_it = lru_map_.find(key);
    if (lru_it != lru_map_.end()) {
        // 移动到前面
        lru_list_.erase(lru_it->second);
        lru_list_.push_front(key);
        lru_map_[key] = lru_list_.begin();
    }
}

template<typename Key, typename Value>
void CacheManager<Key, Value>::SingleLevelCache::updateLFU(const Key& key) {
    auto freq_it = frequency_map_.find(key);
    if (freq_it != frequency_map_.end()) {
        size_t old_freq = freq_it->second.first;
        auto list_it = freq_it->second.second;

        // 从旧频率列表移除
        frequency_lists_[old_freq].erase(list_it);

        // 添加到新频率列表
        size_t new_freq = old_freq + 1;
        frequency_lists_[new_freq].push_front(key);
        frequency_map_[key] = {new_freq, frequency_lists_[new_freq].begin()};

        // 更新最小频率
        if (frequency_lists_[min_frequency_].empty()) {
            min_frequency_++;
        }
    }
}

template<typename Key, typename Value>
std::pair<Key, std::shared_ptr<typename CacheManager<Key, Value>::CacheEntry>>
CacheManager<Key, Value>::SingleLevelCache::evictOne() {
    if (cache_map_.empty()) {
        return {Key{}, nullptr};
    }

    Key evict_key{};

    // switch (policy_) {
    // case EvictionPolicy::LRU: {
    //     if (!lru_list_.empty()) {
    //         evict_key = lru_list_.back();
    //         lru_list_.pop_back();
    //         lru_map_.erase(evict_key);
    //     }
    //     break;
    // }
    // case EvictionPolicy::LFU: {
    //     if (!frequency_lists_[min_frequency_].empty()) {
    //         evict_key = frequency_lists_[min_frequency_].back();
    //         frequency_lists_[min_frequency_].pop_back();
    //         frequency_map_.erase(evict_key);
    //     }
    //     break;
    // }
    // case EvictionPolicy::FIFO: {
    //     if (!lru_list_.empty()) {
    //         evict_key = lru_list_.back();
    //         lru_list_.pop_back();
    //         lru_map_.erase(evict_key);
    //     }
    //     break;
    // }
    // case EvictionPolicy::RANDOM: {
    //     if (!cache_map_.empty()) {
    //         static std::random_device rd;
    //         static std::mt19937 gen(rd());
    //         std::uniform_int_distribution<> dis(0, cache_map_.size() - 1);

    //         auto it = cache_map_.begin();
    //         std::advance(it, dis(gen));
    //         evict_key = it->first;
    //     }
    //     break;
    // }
    // case EvictionPolicy::TTL: {
    //     // 基于时间的淘汰
    //     auto now = std::chrono::steady_clock::now();
    //     auto oldest_time = now;

    //     for (const auto& pair : cache_map_) {
    //         if (pair.second->created_time < oldest_time) {
    //             oldest_time = pair.second->created_time;
    //             evict_key = pair.first;
    //         }
    //     }
    //     break;
    // }
    // default:
    //     if (!cache_map_.empty()) {
    //         evict_key = cache_map_.begin()->first;
    //     }
    //     break;
    // }

    auto entry = cache_map_[evict_key];
    cache_map_.erase(evict_key);

    return {evict_key, entry};
}

// CacheManager 主要实现
template<typename Key, typename Value>
CacheManager<Key, Value>::CacheManager(const Config& config) : config_(config) {
    l1_cache_ = std::make_unique<SingleLevelCache>(config_.l1_capacity, config_.l1_policy);
    l2_cache_ = std::make_unique<SingleLevelCache>(config_.l2_capacity, config_.l2_policy);
    l3_cache_ = std::make_unique<SingleLevelCache>(config_.l3_capacity, config_.l3_policy);

    if (config_.cleanup_interval_ms > 0) {
        startMaintenanceThread();
    }
}

template<typename Key, typename Value>
CacheManager<Key, Value>::~CacheManager() {
    shutdown_.store(true);
    stopMaintenanceThread();
}

template<typename Key, typename Value>
std::shared_ptr<Value> CacheManager<Key, Value>::get(const Key& key) {
    if (shutdown_.load()) {
        return nullptr;
    }

    auto [entry, level] = findEntry(key);

    if (entry) {
        updateStatistics(level, true);

        // 检查是否需要提升
        if (config_.enable_prefetch) {
            checkForPromotion(entry);
        }

        // 解压缩（如果需要）
        if (entry->is_compressed && config_.enable_compression) {
            decompressEntry(entry);
        }

        return std::make_shared<Value>(entry->value);
    }

    // 未命中
    updateStatistics(CacheLevel::L1, false);
    return nullptr;
}

template<typename Key, typename Value>
bool CacheManager<Key, Value>::put(const Key& key, Value&& value, size_t size, CacheLevel level) {
    if (shutdown_.load()) {
        return false;
    }

    auto entry = std::make_shared<CacheEntry>(std::move(value), size, level);

    // 根据指定级别插入
    bool success = false;
    switch (level) {
    case CacheLevel::L1:
        success = l1_cache_->put(key, entry);
        break;
    case CacheLevel::L2:
        success = l2_cache_->put(key, entry);
        break;
    case CacheLevel::L3:
        success = l3_cache_->put(key, entry);
        // L3级别可能需要压缩
        if (success && config_.enable_compression) {
            compressEntry(entry);
        }
        break;
    }

    return success;
}

template<typename Key, typename Value>
bool CacheManager<Key, Value>::remove(const Key& key) {
    std::lock_guard<std::mutex> lock(global_mutex_);

    bool removed = false;
    removed |= l1_cache_->remove(key);
    removed |= l2_cache_->remove(key);
    removed |= l3_cache_->remove(key);

    return removed;
}

template<typename Key, typename Value>
bool CacheManager<Key, Value>::contains(const Key& key) const {
    auto [entry, level] = findEntry(key);
    return entry != nullptr;
}

template<typename Key, typename Value>
void CacheManager<Key, Value>::clear() {
    std::lock_guard<std::mutex> lock(global_mutex_);

    l1_cache_->clear();
    l2_cache_->clear();
    l3_cache_->clear();
}

template<typename Key, typename Value>
std::tuple<size_t, size_t, size_t> CacheManager<Key, Value>::getCacheSizes() const {
    return {l1_cache_->size(), l2_cache_->size(), l3_cache_->size()};
}

template<typename Key, typename Value>
void CacheManager<Key, Value>::prefetch(const std::vector<Key>& keys,
                                        std::function<Value(const Key&)> loader) {
    if (!config_.enable_prefetch) {
        return;
    }

    for (const auto& key : keys) {
        if (!contains(key)) {
            try {
                Value value = loader(key);
                put(key, std::move(value), sizeof(Value), CacheLevel::L3);
                stats_.prefetch_hits.fetch_add(1);
            } catch (...) {
                stats_.prefetch_misses.fetch_add(1);
            }
        }
    }
}

template<typename Key, typename Value>
std::pair<std::shared_ptr<typename CacheManager<Key, Value>::CacheEntry>,
          typename CacheManager<Key, Value>::CacheLevel>
CacheManager<Key, Value>::findEntry(const Key& key) const {
    // 按L1 -> L2 -> L3的顺序查找
    auto entry = l1_cache_->get(key);
    if (entry) {
        return {entry, CacheLevel::L1};
    }

    entry = l2_cache_->get(key);
    if (entry) {
        return {entry, CacheLevel::L2};
    }

    entry = l3_cache_->get(key);
    if (entry) {
        return {entry, CacheLevel::L3};
    }

    return {nullptr, CacheLevel::L1};
}

template<typename Key, typename Value>
void CacheManager<Key, Value>::updateStatistics(CacheLevel level, bool hit) {
    if (!config_.enable_statistics) {
        return;
    }

    if (hit) {
        switch (level) {
        case CacheLevel::L1:
            stats_.l1_hits.fetch_add(1);
            break;
        case CacheLevel::L2:
            stats_.l2_hits.fetch_add(1);
            break;
        case CacheLevel::L3:
            stats_.l3_hits.fetch_add(1);
            break;
        }
    } else {
        stats_.misses.fetch_add(1);
    }
}

template<typename Key, typename Value>
void CacheManager<Key, Value>::startMaintenanceThread() {
    maintenance_running_.store(true);
    maintenance_thread_ = std::thread(&CacheManager::maintenanceThread, this);
}

template<typename Key, typename Value>
void CacheManager<Key, Value>::stopMaintenanceThread() {
    maintenance_running_.store(false);
    maintenance_cv_.notify_all();

    if (maintenance_thread_.joinable()) {
        maintenance_thread_.join();
    }
}

template<typename Key, typename Value>
void CacheManager<Key, Value>::maintenanceThread() {
    std::unique_lock<std::mutex> lock(maintenance_mutex_);

    while (maintenance_running_.load() && !shutdown_.load()) {
        if (maintenance_cv_.wait_for(lock, std::chrono::milliseconds(config_.cleanup_interval_ms))
            == std::cv_status::timeout) {

            // 执行维护任务
            cleanupExpiredEntries();
            forceGarbageCollection();
        }
    }
}

template<typename Key, typename Value>
void CacheManager<Key, Value>::cleanupExpiredEntries() {
    if (config_.ttl_seconds == 0) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto ttl_duration = std::chrono::seconds(config_.ttl_seconds);

    // 清理各级缓存中的过期项
    for (auto* cache : {l1_cache_.get(), l2_cache_.get(), l3_cache_.get()}) {
        auto entries = cache->getAllEntries();
        for (const auto& pair : entries) {
            if (now - pair.second->created_time > ttl_duration) {
                cache->remove(pair.first);
            }
        }
    }
}

template<typename Key, typename Value>
std::string CacheManager<Key, Value>::generateReport() const {
    auto stats = getStatistics();
    auto [l1_size, l2_size, l3_size] = getCacheSizes();

    std::ostringstream oss;
    oss << "=== Cache Manager Report ===\n";
    oss << "Cache Sizes: L1=" << l1_size << ", L2=" << l2_size << ", L3=" << l3_size << "\n";
    oss << "Hit Rates: L1=" << std::fixed << std::setprecision(2)
        << (stats.getL1HitRate() * 100) << "%, Total="
        << (stats.getTotalHitRate() * 100) << "%\n";
    oss << "Total Hits: L1=" << stats.l1_hits
        << ", L2=" << stats.l2_hits
        << ", L3=" << stats.l3_hits << "\n";
    oss << "Misses: " << stats.misses << "\n";
    oss << "Evictions: " << stats.evictions << "\n";
    oss << "Promotions: " << stats.promotions << "\n";
    oss << "Demotions: " << stats.demotions << "\n";

    if (config_.enable_prefetch) {
        oss << "Prefetch Efficiency: " << std::fixed << std::setprecision(2)
        << (stats.getPrefetchEfficiency() * 100) << "%\n";
    }

    return oss.str();
}

// 显式实例化常用类型
template class CacheManager<std::string, std::string>;
template class CacheManager<int, std::vector<uint8_t>>;
template class CacheManager<std::string, std::vector<uint8_t>>;

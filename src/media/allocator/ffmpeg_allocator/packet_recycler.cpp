#include "packet_recycler.h"
#include <algorithm>
#include <sstream>
#include <thread>
#include <cstring>

// 条件包含FFmpeg头文件
#ifdef FFMPEG_AVAILABLE
extern "C" {
#include <libavcodec/packet.h>
#include <libavutil/mem.h>
}
#endif

// RefCountedPacket 实现
PacketRecycler::RefCountedPacket::RefCountedPacket(AVPacket* packet, PacketRecycler* recycler)
    : packet_(packet), recycler_(recycler), ref_count_(1) {
}

PacketRecycler::RefCountedPacket::~RefCountedPacket() {
    release();
}

PacketRecycler::RefCountedPacket::RefCountedPacket(RefCountedPacket&& other) noexcept
    : packet_(other.packet_), recycler_(other.recycler_), ref_count_(other.ref_count_.load()) {
    other.packet_ = nullptr;
    other.recycler_ = nullptr;
    other.ref_count_.store(0);
}

PacketRecycler::RefCountedPacket& PacketRecycler::RefCountedPacket::operator=(RefCountedPacket&& other) noexcept {
    if (this != &other) {
        release();
        packet_ = other.packet_;
        recycler_ = other.recycler_;
        ref_count_.store(other.ref_count_.load());
        other.packet_ = nullptr;
        other.recycler_ = nullptr;
        other.ref_count_.store(0);
    }
    return *this;
}

std::shared_ptr<PacketRecycler::RefCountedPacket> PacketRecycler::RefCountedPacket::share() {
    if (!packet_) return nullptr;

    addRef();
    return std::shared_ptr<RefCountedPacket>(this, [](RefCountedPacket* p) {
        p->release();
    });
}

void PacketRecycler::RefCountedPacket::addRef() {
    ref_count_.fetch_add(1);
}

void PacketRecycler::RefCountedPacket::release() {
    if (packet_ && ref_count_.fetch_sub(1) == 1) {
        // 最后一个引用，回收packet
        if (recycler_) {
#ifdef FFMPEG_AVAILABLE
            SizeCategory category = recycler_->categorizeSize(packet_->size);
            recycler_->recyclePacket(packet_, category);
#endif
        }
        packet_ = nullptr;
        recycler_ = nullptr;
    }
}

// PacketPool 实现
PacketRecycler::PacketPool::PacketPool(SizeCategory category, size_t target_size, size_t capacity)
    : category_(category), target_size_(target_size), capacity_(capacity) {
    available_packets_.reserve(capacity);
}

PacketRecycler::PacketPool::~PacketPool() {
    cleanup(0);  // 清理所有packet
}

AVPacket* PacketRecycler::PacketPool::acquire() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!available_packets_.empty()) {
        AVPacket* packet = available_packets_.back();
        available_packets_.pop_back();
        return packet;
    }

    // 池为空，创建新packet
    if (total_allocated_.load() < capacity_) {
        return createPacket();
    }

    return nullptr;
}

bool PacketRecycler::PacketPool::release(AVPacket* packet) {
    if (!packet) return false;

    std::lock_guard<std::mutex> lock(mutex_);

    if (available_packets_.size() < capacity_) {
#ifdef FFMPEG_AVAILABLE
        // 重置packet但保留缓冲区
        av_packet_unref(packet);
#endif
        available_packets_.push_back(packet);
        return true;
    }

    // 池已满，销毁packet
    destroyPacket(packet);
    return false;
}

std::vector<AVPacket*> PacketRecycler::PacketPool::acquireBatch(size_t count) {
    std::vector<AVPacket*> result;
    result.reserve(count);

    std::lock_guard<std::mutex> lock(mutex_);

    // 先从可用列表获取
    while (!available_packets_.empty() && result.size() < count) {
        result.push_back(available_packets_.back());
        available_packets_.pop_back();
    }

    // 如需要，创建新packet
    while (result.size() < count && total_allocated_.load() < capacity_) {
        AVPacket* packet = createPacket();
        if (packet) {
            result.push_back(packet);
        } else {
            break;
        }
    }

    return result;
}

size_t PacketRecycler::PacketPool::releaseBatch(const std::vector<AVPacket*>& packets) {
    if (packets.empty()) return 0;

    std::lock_guard<std::mutex> lock(mutex_);

    size_t released = 0;
    for (AVPacket* packet : packets) {
        if (!packet) continue;

        if (available_packets_.size() < capacity_) {
#ifdef FFMPEG_AVAILABLE
            av_packet_unref(packet);
#endif
            available_packets_.push_back(packet);
            ++released;
        } else {
            destroyPacket(packet);
        }
    }

    return released;
}

size_t PacketRecycler::PacketPool::available() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return available_packets_.size();
}

AVPacket* PacketRecycler::PacketPool::createPacket() {
#ifdef FFMPEG_AVAILABLE
    AVPacket* packet = av_packet_alloc();
    if (!packet) return nullptr;

    if (!allocateBuffer(packet, target_size_)) {
        av_packet_free(&packet);
        return nullptr;
    }

    total_allocated_.fetch_add(1);
    return packet;
#else
    return nullptr;
#endif
}

void PacketRecycler::PacketPool::destroyPacket(AVPacket* packet) {
    if (!packet) return;

#ifdef FFMPEG_AVAILABLE
    av_packet_free(&packet);
#endif

    if (total_allocated_.load() > 0) {
        total_allocated_.fetch_sub(1);
    }
}

bool PacketRecycler::PacketPool::allocateBuffer(AVPacket* packet, size_t size) {
    if (!packet) return false;

#ifdef FFMPEG_AVAILABLE
    return av_new_packet(packet, static_cast<int>(size)) >= 0;
#else
    return false;
#endif
}

size_t PacketRecycler::PacketPool::getMemoryUsage() const {
    return total_allocated_.load() * target_size_;
}

void PacketRecycler::PacketPool::cleanup(size_t keep_count) {
    std::lock_guard<std::mutex> lock(mutex_);

    while (available_packets_.size() > keep_count) {
        AVPacket* packet = available_packets_.back();
        available_packets_.pop_back();
        destroyPacket(packet);
    }
}

// PacketRecycler 实现
PacketRecycler::PacketRecycler(const Config& config) : config_(config) {
    if (config_.cleanup_interval_ms > 0) {
        startCleanupThread();
    }
}

PacketRecycler::~PacketRecycler() {
    shutdown_.store(true);
    stopCleanupThread();

    std::lock_guard<std::mutex> lock(pools_mutex_);
    pools_.clear();
}

PacketRecycler::PacketPtr PacketRecycler::allocatePacket(size_t size) {
    if (shutdown_.load()) {
        return nullptr;
    }

    SizeCategory category = categorizeSize(size);
    size_t target_size = std::max(size, getCategorySuggestedSize(category));

    // 尝试从池中获取
    auto pool = getOrCreatePool(category, target_size);
    AVPacket* packet = nullptr;

    if (pool) {
        packet = pool->acquire();
        if (packet) {
            updateStatistics(category, size, true, true);
            stats_.pool_hits.fetch_add(1);

            if (config_.enable_reference_counting) {
                return std::make_unique<RefCountedPacket>(packet, this);
            } else {
                return std::make_unique<RefCountedPacket>(packet, nullptr);
            }
        }
    }

    // 池中无可用packet，直接分配
    stats_.pool_misses.fetch_add(1);

#ifdef FFMPEG_AVAILABLE
    packet = av_packet_alloc();
    if (packet && av_new_packet(packet, static_cast<int>(size)) >= 0) {
        updateStatistics(category, size, true, false);

        if (config_.enable_reference_counting) {
            return std::make_unique<RefCountedPacket>(packet, this);
        } else {
            return std::make_unique<RefCountedPacket>(packet, nullptr);
        }
    }

    if (packet) {
        av_packet_free(&packet);
    }
#endif

    return nullptr;
}

std::vector<PacketRecycler::PacketPtr> PacketRecycler::allocatePacketBatch(const std::vector<size_t>& sizes) {
    std::vector<PacketPtr> result;
    result.reserve(sizes.size());

    if (!config_.enable_batch_recycling) {
        // 逐个分配
        for (size_t size : sizes) {
            auto packet = allocatePacket(size);
            if (packet) {
                result.push_back(std::move(packet));
            }
        }
        return result;
    }

    // 按类别分组批量分配
    std::unordered_map<SizeCategory, std::vector<size_t>> category_groups;
    for (size_t size : sizes) {
        SizeCategory category = categorizeSize(size);
        category_groups[category].push_back(size);
    }

    for (const auto& group : category_groups) {
        SizeCategory category = group.first;
        const auto& group_sizes = group.second;

        size_t target_size = getCategorySuggestedSize(category);
        auto pool = getOrCreatePool(category, target_size);

        if (pool) {
            auto packets = pool->acquireBatch(group_sizes.size());
            for (size_t i = 0; i < packets.size() && i < group_sizes.size(); ++i) {
                if (packets[i]) {
                    updateStatistics(category, group_sizes[i], true, true);

                    if (config_.enable_reference_counting) {
                        result.push_back(std::make_unique<RefCountedPacket>(packets[i], this));
                    } else {
                        result.push_back(std::make_unique<RefCountedPacket>(packets[i], nullptr));
                    }
                }
            }
        }
    }

    return result;
}

PacketRecycler::SizeCategory PacketRecycler::categorizeSize(size_t size) const {
    if (size < PacketSizes::TINY_MAX) {
        return SizeCategory::TINY;
    } else if (size < PacketSizes::SMALL_MAX) {
        return SizeCategory::SMALL;
    } else if (size < PacketSizes::MEDIUM_MAX) {
        return SizeCategory::MEDIUM;
    } else if (size < PacketSizes::LARGE_MAX) {
        return SizeCategory::LARGE;
    } else {
        return SizeCategory::EXTRA_LARGE;
    }
}

size_t PacketRecycler::getCategorySuggestedSize(SizeCategory category) const {
    switch (category) {
    case SizeCategory::TINY:   return PacketSizes::AUDIO_TYPICAL;
    case SizeCategory::SMALL:  return PacketSizes::VIDEO_SD_TYPICAL;
    case SizeCategory::MEDIUM: return PacketSizes::VIDEO_HD_TYPICAL;
    case SizeCategory::LARGE:  return PacketSizes::VIDEO_4K_TYPICAL;
    case SizeCategory::EXTRA_LARGE:   return PacketSizes::LARGE_MAX;
    default: return PacketSizes::SMALL_MAX;
    }
}

std::shared_ptr<PacketRecycler::PacketPool> PacketRecycler::getOrCreatePool(
    SizeCategory category, size_t target_size) {

    std::lock_guard<std::mutex> lock(pools_mutex_);

    auto& category_pools = pools_[category];
    auto it = category_pools.find(target_size);

    if (it != category_pools.end()) {
        return it->second;
    }

    // 检查池数量限制
    if (category_pools.size() >= config_.max_pools_per_category) {
        return nullptr;
    }

    // 创建新池
    auto pool = std::make_shared<PacketPool>(category, target_size, config_.packets_per_pool);
    category_pools[target_size] = pool;

    return pool;
}

void PacketRecycler::recyclePacket(AVPacket* packet, SizeCategory category) {
    if (!packet || shutdown_.load()) {
        return;
    }

    size_t target_size = getCategorySuggestedSize(category);
    auto pool = getOrCreatePool(category, target_size);

    if (pool && pool->release(packet)) {
        updateStatistics(category, packet->size, false, true);
    } else {
        updateStatistics(category, packet->size, false, false);
#ifdef FFMPEG_AVAILABLE
        av_packet_free(&packet);
#endif
    }
}

void PacketRecycler::updateStatistics(SizeCategory category, size_t size, bool is_allocation, bool from_pool) {
    if (!config_.enable_statistics) return;

    if (is_allocation) {
        stats_.total_allocated.fetch_add(1);
        stats_.current_memory_usage.fetch_add(size);
        stats_.category_counts[static_cast<int>(category)].fetch_add(1);

        // 更新峰值内存使用
        size_t current = stats_.current_memory_usage.load();
        size_t old_peak = stats_.peak_memory_usage.load();
        while (current > old_peak &&
               !stats_.peak_memory_usage.compare_exchange_weak(old_peak, current)) {
            // 循环直到成功更新峰值
        }
    } else {
        stats_.total_recycled.fetch_add(1);
        if (stats_.current_memory_usage.load() >= size) {
            stats_.current_memory_usage.fetch_sub(size);
        }
    }

    if (is_allocation) {
        checkMemoryPressure();
    }
}

void PacketRecycler::checkMemoryPressure() {
    size_t current = stats_.current_memory_usage.load();

    if (current > config_.max_total_memory * config_.memory_pressure_threshold) {
        if (memory_pressure_callback_) {
            memory_pressure_callback_(current, config_.max_total_memory);
        }

        // 触发部分清理
        forceGarbageCollection();
    }
}

void PacketRecycler::forceGarbageCollection() {
    std::lock_guard<std::mutex> lock(pools_mutex_);

    for (auto& category_pair : pools_) {
        for (auto& pool_pair : category_pair.second) {
            auto& pool = pool_pair.second;
            // 保留少量packet，清理其余
            pool->cleanup(config_.packets_per_pool / 4);
        }
    }
}

void PacketRecycler::startCleanupThread() {
    cleanup_running_.store(true);
    cleanup_thread_ = std::thread(&PacketRecycler::cleanupThread, this);
}

void PacketRecycler::stopCleanupThread() {
    cleanup_running_.store(false);
    cleanup_cv_.notify_all();

    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
}

void PacketRecycler::cleanupThread() {
    std::unique_lock<std::mutex> lock(cleanup_mutex_);

    while (cleanup_running_.load() && !shutdown_.load()) {
        if (cleanup_cv_.wait_for(lock, std::chrono::milliseconds(config_.cleanup_interval_ms))
            == std::cv_status::timeout) {

            // 执行定期清理
            forceGarbageCollection();
        }
    }
}

std::string PacketRecycler::getMemoryReport() const {
    auto stats = getStatistics();
    std::ostringstream oss;

    oss << "=== Packet Recycler Report ===\n";
    oss << "Total Allocated: " << stats.total_allocated.load() << "\n";
    oss << "Total Recycled: " << stats.total_recycled.load() << "\n";
    oss << "Recycling Rate: " << (stats.getRecyclingRate() * 100) << "%\n";
    oss << "Pool Hit Rate: " << (stats.getPoolHitRate() * 100) << "%\n";
    oss << "Current Memory: " << stats.current_memory_usage.load() << " bytes\n";
    oss << "Peak Memory: " << stats.peak_memory_usage.load() << " bytes\n";

    return oss.str();
}

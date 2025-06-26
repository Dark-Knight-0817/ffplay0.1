#include "memory_tracker.h"
#include <algorithm>
#include <sstream>
#include <iomanip>

MemoryTracker::MemoryTracker(const Config& config)
    :config_(config)
{
    if(config_.enable_history){
        startHistoryRecording();
    }
}

MemoryTracker::~MemoryTracker()
{
    shutdown_.store(true);

    if(config_.enable_history){
        stopHistoryRecording();
    }

    // 在析构时检查泄漏
    if(config_.enable_leadk_detection){
        auto leaks = detectLeaks();
        if(!leaks.empty()){
            // 这里应该记录日志，表示发现内存泄漏
            // 在实际应用中，可能需要写入日志文件或触发断言
        }
    }
}

void MemoryTracker::recordAllocation(void* ptr,size_t size, const std::string& location)
{
    if(!ptr || shutdown_.load()){
        return ;
    }

    // 更新统计信息
    stats_.allocation_count.fetch_add(1);
    stats_.total_allocated.fetch_add(size);

    size_t old_usage = stats_.current_usage.fetch_add(size);
    size_t new_usage = old_usage + size;

    // 更新峰值使用量
    size_t old_peak = stats_.peak_usage.load();
    while(new_usage > old_peak
               && !stats_.peak_usage.compare_exchange_weak(old_peak, new_usage))
    {
        // 循环直到成功更新峰值
    }

    // 检查预警
    if(new_usage >config_.alert_threshold){
        checkAndAlert(new_usage);
    }

    // 记录分配信息（如果启用泄漏检测）
    if(config_.enable_leadk_detection){
        std::lock_guard<std::mutex> lock(allocations_mutex_);

        // 检查容量限制
        if(active_allocations_.size() >= config_.max_allocations){
            // 移除最老的记录
            auto oldest = std::min_element(active_allocations_.begin(),active_allocations_.end(),
                                           [](const auto& a, const auto& b){
                return a.second.timestamp < b.second.timestamp;
            });
            if(oldest != active_allocations_.end()){
                active_allocations_.erase(oldest);
            }
        }

        active_allocations_.emplace(ptr, AllocationInfo(ptr, size, location));
    }

    // 更新热点统计
    if(!location.empty()){
        std::lock_guard<std::mutex> lock(hotspots_mutex_);
        allocation_hotspots_[location]++;
    }
}

bool MemoryTracker::recordDeallocation(void* ptr)
{
    if(!ptr || shutdown_.load()){
        return false;
    }

    bool found = false;
    size_t size = 0;

    // 查找并移除分配记录
    if(config_.enable_leadk_detection){
        std::lock_guard<std::mutex> lock(allocations_mutex_);
        auto it = active_allocations_.find(ptr);
        if(it != active_allocations_.end()){
            size = it->second.size;
            active_allocations_.erase(it);
            found = true;
        }
    }

    if (found) {
        // 更新统计信息
        stats_.free_count.fetch_add(1);
        stats_.total_freed.fetch_add(size);
        stats_.current_usage.fetch_sub(size);
    } else {
        // 记录可能的重复释放
        stats_.free_count.fetch_add(1);
    }

    return found;
}

std::vector<MemoryTracker::AllocationInfo> MemoryTracker::detectLeaks() const {
    std::vector<AllocationInfo> leaks;

    if (!config_.enable_leak_detection) {
        return leaks;
    }

    std::lock_guard<std::mutex> lock(allocations_mutex_);

    auto now = std::chrono::steady_clock::now();
    auto leak_threshold = std::chrono::minutes(5); // 5分钟未释放视为潜在泄漏

    for (const auto& pair : active_allocations_) {
        const auto& info = pair.second;
        if (now - info.timestamp > leak_threshold) {
            leaks.push_back(info);
        }
    }

    // 按分配时间排序
    std::sort(leaks.begin(), leaks.end(),
              [](const AllocationInfo& a, const AllocationInfo& b) {
                  return a.timestamp < b.timestamp;
              });

    return leaks;
}

std::unordered_map<std::string, size_t> MemoryTracker::getSizeDistribution() const {
    std::unordered_map<std::string, size_t> distribution;

    if (!config_.enable_leak_detection) {
        return distribution;
    }

    std::lock_guard<std::mutex> lock(allocations_mutex_);

    for (const auto& pair : active_allocations_) {
        std::string category = categorizeSize(pair.second.size);
        distribution[category]++;
    }

    return distribution;
}

std::vector<std::pair<std::string, size_t>> MemoryTracker::getHotspots(size_t top_n) const {
    std::lock_guard<std::mutex> lock(hotspots_mutex_);

    std::vector<std::pair<std::string, size_t>> hotspots(
        allocation_hotspots_.begin(), allocation_hotspots_.end());

    // 按分配次数降序排序
    std::sort(hotspots.begin(), hotspots.end(),
              [](const auto& a, const auto& b) {
                  return a.second > b.second;
              });

    // 只返回前top_n个
    if (hotspots.size() > top_n) {
        hotspots.resize(top_n);
    }

    return hotspots;
}

std::vector<MemoryTracker::Snapshot> MemoryTracker::getHistory() const {
    std::lock_guard<std::mutex> lock(history_mutex_);
    return history_;
}

std::string MemoryTracker::generateReport() const {
    auto stats = getStatistics();
    std::ostringstream oss;

    oss << "=== Memory Tracker Report ===\n";
    oss << "Generated at: " << std::chrono::duration_cast<std::chrono::seconds>(
                                   std::chrono::steady_clock::now().time_since_epoch()).count() << " seconds\n\n";

    // 基本统计
    oss << "--- Basic Statistics ---\n";
    oss << "Current Usage: " << stats.current_usage.load() << " bytes\n";
    oss << "Peak Usage: " << stats.peak_usage.load() << " bytes\n";
    oss << "Total Allocated: " << stats.total_allocated.load() << " bytes\n";
    oss << "Total Freed: " << stats.total_freed.load() << " bytes\n";
    oss << "Allocation Count: " << stats.allocation_count.load() << "\n";
    oss << "Free Count: " << stats.free_count.load() << "\n";
    oss << "Average Allocation Size: " << std::fixed << std::setprecision(2)
        << stats.getAverageAllocationSize() << " bytes\n";
    oss << "Memory Efficiency: " << std::fixed << std::setprecision(2)
        << (stats.getMemoryEfficiency() * 100) << "%\n\n";

    // 泄漏检测
    if (config_.enable_leak_detection) {
        auto leaks = detectLeaks();
        oss << "--- Leak Detection ---\n";
        oss << "Active Allocations: " << active_allocations_.size() << "\n";
        oss << "Potential Leaks: " << leaks.size() << "\n";

        if (!leaks.empty()) {
            oss << "Top 5 Leaks:\n";
            for (size_t i = 0; i < std::min(leaks.size(), size_t(5)); ++i) {
                const auto& leak = leaks[i];
                oss << "  " << (i+1) << ". Size: " << leak.size
                    << " bytes, Location: " << leak.location << "\n";
            }
        }
        oss << "\n";
    }

    // 大小分布
    auto distribution = getSizeDistribution();
    if (!distribution.empty()) {
        oss << "--- Size Distribution ---\n";
        for (const auto& pair : distribution) {
            oss << pair.first << ": " << pair.second << " allocations\n";
        }
        oss << "\n";
    }

    // 热点分析
    auto hotspots = getHotspots(5);
    if (!hotspots.empty()) {
        oss << "--- Top 5 Allocation Hotspots ---\n";
        for (size_t i = 0; i < hotspots.size(); ++i) {
            oss << (i+1) << ". " << hotspots[i].first
                << ": " << hotspots[i].second << " allocations\n";
        }
        oss << "\n";
    }

    // 历史趋势
    auto history = getHistory();
    if (history.size() > 1) {
        oss << "--- Memory Usage Trend (Last 10 snapshots) ---\n";
        size_t start = history.size() > 10 ? history.size() - 10 : 0;
        for (size_t i = start; i < history.size(); ++i) {
            const auto& snapshot = history[i];
            auto time_since_start = std::chrono::duration_cast<std::chrono::seconds>(
                                        snapshot.timestamp - history[0].timestamp).count();
            oss << "T+" << time_since_start << "s: "
                << snapshot.current_usage << " bytes\n";
        }
    }

    return oss.str();
}

std::string MemoryTracker::generateCSVData() const {
    std::ostringstream oss;
    oss << "timestamp,current_usage,allocation_count,free_count\n";

    std::lock_guard<std::mutex> lock(history_mutex_);
    for (const auto& snapshot : history_) {
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                             snapshot.timestamp.time_since_epoch()).count();
        oss << timestamp << "," << snapshot.current_usage << ","
            << snapshot.allocation_count << "," << snapshot.free_count << "\n";
    }

    return oss.str();
}

void MemoryTracker::setAlertCallback(AlertCallback callback) {
    alert_callback_ = std::move(callback);
}

void MemoryTracker::reset() {
    // 重置统计信息
    stats_.total_allocated.store(0);
    stats_.total_freed.store(0);
    stats_.current_usage.store(0);
    stats_.peak_usage.store(0);
    stats_.allocation_count.store(0);
    stats_.free_count.store(0);
    stats_.leak_count.store(0);

    // 清空分配记录
    if (config_.enable_leak_detection) {
        std::lock_guard<std::mutex> lock(allocations_mutex_);
        active_allocations_.clear();
    }

    // 清空热点统计
    {
        std::lock_guard<std::mutex> lock(hotspots_mutex_);
        allocation_hotspots_.clear();
    }

    // 清空历史记录
    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        history_.clear();
    }

    alert_triggered_.store(false);
}

void MemoryTracker::startHistoryRecording() {
    if (recording_history_.load()) {
        return;
    }

    recording_history_.store(true);
    history_thread_ = std::thread(&MemoryTracker::historyRecordingThread, this);
}

void MemoryTracker::stopHistoryRecording() {
    if (!recording_history_.load()) {
        return;
    }

    recording_history_.store(false);
    history_cv_.notify_all();

    if (history_thread_.joinable()) {
        history_thread_.join();
    }
}

void MemoryTracker::takeSnapshot() {
    Snapshot snapshot;
    snapshot.current_usage = stats_.current_usage.load();
    snapshot.allocation_count = stats_.allocation_count.load();
    snapshot.free_count = stats_.free_count.load();

    std::lock_guard<std::mutex> lock(history_mutex_);
    history_.push_back(snapshot);

    // 清理过期记录
    if (history_.size() > config_.max_history_size) {
        history_.erase(history_.begin(),
                       history_.begin() + (history_.size() - config_.max_history_size));
    }
}

bool MemoryTracker::isHealthy() const {
    auto stats = getStatistics();

    // 检查内存使用是否正常
    if (stats.current_usage.load() > config_.alert_threshold) {
        return false;
    }

    // 检查是否有严重的内存泄漏
    if (config_.enable_leak_detection) {
        auto leaks = detectLeaks();
        if (leaks.size() > 100) { // 超过100个潜在泄漏
            return false;
        }
    }

    return true;
}

void MemoryTracker::historyRecordingThread() {
    std::unique_lock<std::mutex> lock(history_mutex_);

    while (recording_history_.load() && !shutdown_.load()) {
        // 等待指定间隔或停止信号
        if (history_cv_.wait_for(lock, config_.history_interval) == std::cv_status::timeout) {
            lock.unlock();
            takeSnapshot();
            lock.lock();
        }
    }
}

void MemoryTracker::checkAndAlert(size_t current_usage) {
    if (alert_callback_ && !alert_triggered_.load()) {
        alert_triggered_.store(true);

        std::string message = "Memory usage exceeded threshold: " +
                              std::to_string(current_usage) + " bytes";

        // 在新线程中调用回调，避免阻塞
        std::thread([this, message, current_usage]() {
            alert_callback_(message, current_usage, config_.alert_threshold);

            // 重置预警状态，允许后续预警
            std::this_thread::sleep_for(std::chrono::minutes(1));
            alert_triggered_.store(false);
        }).detach();
    }
}

void MemoryTracker::cleanupHistory() {
    std::lock_guard<std::mutex> lock(history_mutex_);

    if (history_.size() <= config_.max_history_size) {
        return;
    }

    // 保留最新的记录
    size_t to_remove = history_.size() - config_.max_history_size;
    history_.erase(history_.begin(), history_.begin() + to_remove);
}

std::string MemoryTracker::categorizeSize(size_t size) const {
    if (size <= 64) return "Tiny (≤64B)";
    if (size <= 1024) return "Small (64B-1KB)";
    if (size <= 64 * 1024) return "Medium (1KB-64KB)";
    if (size <= 1024 * 1024) return "Large (64KB-1MB)";
    return "Huge (>1MB)";
}

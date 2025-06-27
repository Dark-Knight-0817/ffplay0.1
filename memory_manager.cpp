#include "memory_manager.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <chrono>

MemoryManager::MemoryManager(const Config& config) : config_(config) {
}

MemoryManager::~MemoryManager() {
    shutdown();
}

bool MemoryManager::initialize() {
    if (initialized_.load()) {
        return true;
    }

    std::lock_guard<std::mutex> lock(config_mutex_);

    // 应用策略配置
    applyStrategy(config_.strategy);

    // 初始化各个组件
    if (!initializeComponents()) {
        return false;
    }

    // 启动后台线程
    if (config_.enable_auto_optimization || config_.enable_global_tracking) {
        startBackgroundThreads();
    }

    initialized_.store(true);
    return true;
}

void MemoryManager::shutdown() {
    if (!initialized_.load()) {
        return;
    }

    shutdown_.store(true);

    // 停止后台线程
    stopBackgroundThreads();

    // 清理组件
    {
        std::lock_guard<std::mutex> lock(config_mutex_);
        frame_allocator_.reset();
        packet_recycler_.reset();
        memory_pool_.reset();
        memory_tracker_.reset();
    }

    {
        std::lock_guard<std::mutex> lock(cache_managers_mutex_);
        cache_managers_.clear();
    }

    initialized_.store(false);
}

bool MemoryManager::initializeComponents() {
    try {
        // 初始化内存池
        if (config_.use_memory_pool) {
            MemoryPool::Config pool_config;

            switch (config_.strategy) {
            case Strategy::PERFORMANCE:
                pool_config.initial_pool_size = 64 * 1024 * 1024;  // 64MB
                pool_config.max_pool_size = 512 * 1024 * 1024;    // 512MB
                break;
            case Strategy::MEMORY_SAVING:
                pool_config.initial_pool_size = 4 * 1024 * 1024;   // 4MB
                pool_config.max_pool_size = 32 * 1024 * 1024;     // 32MB
                break;
            case Strategy::BALANCED:
            default:
                pool_config.initial_pool_size = 16 * 1024 * 1024;  // 16MB
                pool_config.max_pool_size = 128 * 1024 * 1024;    // 128MB
                break;
            }

            pool_config.enable_statistics = config_.enable_global_tracking;
            memory_pool_ = std::make_unique<MemoryPool>(pool_config);
        }

        // 初始化内存跟踪器
        if (config_.enable_global_tracking) {
            MemoryTracker::Config tracker_config;
            tracker_config.enable_leak_detection = true;
            tracker_config.enable_statistics = true;
            tracker_config.enable_history = true;
            tracker_config.alert_threshold = config_.max_total_memory * config_.memory_pressure_threshold;

            memory_tracker_ = std::make_unique<MemoryTracker>(tracker_config);

            // 设置内存压力回调
            memory_tracker_->setAlertCallback([this](const std::string& msg, size_t current, size_t threshold) {
                if (pressure_callback_) {
                    PressureEvent event(PressureLevel::HIGH, current, config_.max_total_memory, msg);
                    pressure_callback_(event);
                }
                handleMemoryPressure(PressureLevel::HIGH);
            });
        }

        // 初始化帧分配器
        if (config_.use_frame_allocator) {
            FrameAllocator& MemoryManager::getFrameAllocator() {
                if (!frame_allocator_) {
                    throw std::runtime_error("FrameAllocator not initialized");
                }
                return *frame_allocator_;
            }

            PacketRecycler& MemoryManager::getPacketRecycler() {
                if (!packet_recycler_) {
                    throw std::runtime_error("PacketRecycler not initialized");
                }
                return *packet_recycler_;
            }

            template<typename Key, typename Value>
            CacheManager<Key, Value>& MemoryManager::getCacheManager() {
                std::lock_guard<std::mutex> lock(cache_managers_mutex_);

                std::string type_name = typeid(CacheManager<Key, Value>).name();

                auto it = cache_managers_.find(type_name);
                if (it != cache_managers_.end()) {
                    return *static_cast<CacheManager<Key, Value>*>(it->second.get());
                }

                // 创建新的缓存管理器
                typename CacheManager<Key, Value>::Config cache_config;

                switch (config_.strategy) {
                case Strategy::PERFORMANCE:
                    cache_config.l1_capacity = 2000;
                    cache_config.l2_capacity = 10000;
                    cache_config.l3_capacity = 50000;
                    break;
                case Strategy::MEMORY_SAVING:
                    cache_config.l1_capacity = 200;
                    cache_config.l2_capacity = 1000;
                    cache_config.l3_capacity = 5000;
                    break;
                case Strategy::BALANCED:
                default:
                    cache_config.l1_capacity = 1000;
                    cache_config.l2_capacity = 5000;
                    cache_config.l3_capacity = 20000;
                    break;
                }

                cache_config.enable_statistics = config_.enable_global_tracking;

                auto cache_manager = std::make_shared<CacheManager<Key, Value>>(cache_config);
                cache_managers_[type_name] = std::static_pointer_cast<void>(cache_manager);

                return *cache_manager;
            }

            void* MemoryManager::allocate(size_t size, size_t alignment, const std::string& hint) {
                if (!initialized_.load()) {
                    return nullptr;
                }

                auto start_time = std::chrono::high_resolution_clock::now();
                void* ptr = nullptr;

                // 根据提示选择分配策略
                if (hint.find("frame") != std::string::npos && frame_allocator_) {
                    // 帧分配器处理
                    // 这里需要更多参数，简化处理
                    ptr = memory_pool_ ? memory_pool_->allocate(size, alignment) : std::malloc(size);
                } else if (memory_pool_) {
                    // 使用内存池
                    ptr = memory_pool_->allocate(size, alignment);
                } else {
                    // 回退到系统分配
                    ptr = alignment > 0 ? std::aligned_alloc(alignment, size) : std::malloc(size);
                }

                // 记录分配
                if (ptr && memory_tracker_) {
                    std::string location = hint.empty() ? "MemoryManager::allocate" : hint;
                    memory_tracker_->recordAllocation(ptr, size, location);
                }

                // 更新性能统计
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

                {
                    std::lock_guard<std::mutex> lock(timing_mutex_);
                    allocation_time_accumulator_ += duration.count();
                    allocation_count_++;
                    last_allocation_time_ = end_time;
                }

                return ptr;
            }

            void MemoryManager::deallocate(void* ptr) {
                if (!ptr || !initialized_.load()) {
                    return;
                }

                auto start_time = std::chrono::high_resolution_clock::now();

                // 记录释放
                if (memory_tracker_) {
                    memory_tracker_->recordDeallocation(ptr);
                }

                // 尝试各种释放方式
                bool freed = false;

                if (memory_pool_) {
                    memory_pool_->deallocate(ptr);
                    freed = true;
                } else {
                    std::free(ptr);
                    freed = true;
                }

                // 更新性能统计
                auto end_time = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

                {
                    std::lock_guard<std::mutex> lock(timing_mutex_);
                    deallocation_time_accumulator_ += duration.count();
                    deallocation_count_++;
                    last_deallocation_time_ = end_time;
                }
            }

            MemoryManager::GlobalStatistics MemoryManager::getGlobalStatistics() const {
                std::lock_guard<std::mutex> lock(stats_mutex_);

                GlobalStatistics stats = global_stats_;

                // 收集各组件统计
                if (memory_pool_) {
                    stats.pool_stats = memory_pool_->getStatistics();
                }

                if (memory_tracker_) {
                    stats.tracker_stats = memory_tracker_->getStatistics();
                }

                if (frame_allocator_) {
                    stats.frame_stats = frame_allocator_->getStatistics();
                }

                if (packet_recycler_) {
                    stats.packet_stats = packet_recycler_->getStatistics();
                }

                // 计算性能指标
                {
                    std::lock_guard<std::mutex> timing_lock(timing_mutex_);
                    if (allocation_count_ > 0) {
                        stats.avg_allocation_time_ms = allocation_time_accumulator_ / allocation_count_ / 1000.0;
                    }
                    if (deallocation_count_ > 0) {
                        stats.avg_deallocation_time_ms = deallocation_time_accumulator_ / deallocation_count_ / 1000.0;
                    }

                    // 计算分配速率（每秒）
                    auto now = std::chrono::steady_clock::now();
                    if (allocation_count_ > 0) {
                        auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_allocation_time_);
                        if (duration.count() > 0) {
                            stats.allocation_rate_per_second = allocation_count_ / duration.count();
                        }
                    }
                }

                return stats;
            }

            MemoryManager::PressureLevel MemoryManager::getCurrentPressureLevel() const {
                return current_pressure_level_.load();
            }

            void MemoryManager::forceGarbageCollection() {
                if (!initialized_.load()) {
                    return;
                }

                // 对所有组件执行垃圾回收
                if (memory_pool_) {
                    memory_pool_->defragment();
                }

                if (frame_allocator_) {
                    frame_allocator_->forceGarbageCollection();
                }

                if (packet_recycler_) {
                    packet_recycler_->forceGarbageCollection();
                }

                // 清理缓存管理器
                {
                    std::lock_guard<std::mutex> lock(cache_managers_mutex_);
                    // 这里需要为每个缓存管理器调用清理方法
                    // 由于类型擦除，实现会比较复杂，简化处理
                }
            }

            void MemoryManager::optimizeConfiguration() {
                if (!config_.enable_auto_optimization) {
                    return;
                }

                auto stats = getGlobalStatistics();

                // 根据统计信息调整配置
                double memory_efficiency = stats.pool_stats.getHitRate();
                double current_usage_ratio = static_cast<double>(stats.total_used_memory) / config_.max_total_memory;

                if (memory_efficiency < 0.5) {
                    // 命中率低，可能需要增加池大小
                    if (current_usage_ratio < 0.7) {
                        // 内存充足，可以增加池大小
                        // 这里可以动态调整各组件配置
                    }
                }

                if (current_usage_ratio > 0.9) {
                    // 内存紧张，触发更激进的回收
                    forceGarbageCollection();
                }
            }

            std::string MemoryManager::generateComprehensiveReport() const {
                auto stats = getGlobalStatistics();
                std::ostringstream oss;

                oss << "=== Memory Manager Comprehensive Report ===\n";
                oss << "Strategy: ";
                switch (config_.strategy) {
                case Strategy::PERFORMANCE: oss << "Performance"; break;
                case Strategy::MEMORY_SAVING: oss << "Memory Saving"; break;
                case Strategy::BALANCED: oss << "Balanced"; break;
                case Strategy::CUSTOM: oss << "Custom"; break;
                }
                oss << "\n";

                oss << "Scenario: ";
                switch (config_.scenario) {
                case ScenarioType::SINGLE_STREAM: oss << "Single Stream"; break;
                case ScenarioType::MULTI_STREAM: oss << "Multi Stream"; break;
                case ScenarioType::REAL_TIME: oss << "Real Time"; break;
                case ScenarioType::BATCH_PROCESSING: oss << "Batch Processing"; break;
                case ScenarioType::LOW_LATENCY: oss << "Low Latency"; break;
                case ScenarioType::HIGH_THROUGHPUT: oss << "High Throughput"; break;
                }
                oss << "\n\n";

                oss << "--- Global Statistics ---\n";
                oss << "Total Allocated Memory: " << stats.total_allocated_memory << " bytes\n";
                oss << "Total Used Memory: " << stats.total_used_memory << " bytes\n";
                oss << "Peak Memory Usage: " << stats.peak_memory_usage << " bytes\n";
                oss << "Overall Efficiency: " << std::fixed << std::setprecision(2)
                    << (stats.overall_efficiency * 100) << "%\n";
                oss << "Fragmentation Rate: " << std::fixed << std::setprecision(2)
                    << (stats.fragmentation_rate * 100) << "%\n";
                oss << "Average Allocation Time: " << std::fixed << std::setprecision(3)
                    << stats.avg_allocation_time_ms << " ms\n";
                oss << "Average Deallocation Time: " << std::fixed << std::setprecision(3)
                    << stats.avg_deallocation_time_ms << " ms\n";
                oss << "Allocation Rate: " << stats.allocation_rate_per_second << " /sec\n\n";

                // 组件报告
                if (memory_pool_) {
                    oss << "--- Memory Pool Report ---\n";
                    oss << memory_pool_->getReport() << "\n";
                }

                if (memory_tracker_) {
                    oss << "--- Memory Tracker Report ---\n";
                    oss << memory_tracker_->generateReport() << "\n";
                }

                if (packet_recycler_) {
                    oss << "--- Packet Recycler Report ---\n";
                    oss << packet_recycler_->getMemoryReport() << "\n";
                }

                return oss.str();
            }

            void MemoryManager::setMemoryPressureCallback(std::function<void(const PressureEvent&)> callback) {
                pressure_callback_ = std::move(callback);
            }

            void MemoryManager::setPerformanceCallback(std::function<void(const GlobalStatistics&)> callback) {
                performance_callback_ = std::move(callback);
            }

            void MemoryManager::enableComponent(const std::string& component_name, bool enable) {
                std::lock_guard<std::mutex> lock(config_mutex_);

                if (component_name == "memory_pool") {
                    config_.use_memory_pool = enable;
                } else if (component_name == "frame_allocator") {
                    config_.use_frame_allocator = enable;
                } else if (component_name == "packet_recycler") {
                    config_.use_packet_recycler = enable;
                } else if (component_name == "cache_manager") {
                    config_.use_cache_manager = enable;
                } else if (component_name == "object_pools") {
                    config_.use_object_pools = enable;
                }

                // 需要重新初始化才能生效
            }

            void MemoryManager::setMemoryLimit(size_t max_bytes) {
                std::lock_guard<std::mutex> lock(config_mutex_);
                config_.max_total_memory = max_bytes;
            }

            std::vector<std::pair<std::chrono::steady_clock::time_point, size_t>>
            MemoryManager::getMemoryUsageTrend(int duration_minutes) const {
                std::lock_guard<std::mutex> lock(stats_mutex_);

                auto now = std::chrono::steady_clock::now();
                auto cutoff_time = now - std::chrono::minutes(duration_minutes);

                std::vector<std::pair<std::chrono::steady_clock::time_point, size_t>> trend;

                for (const auto& record : memory_history_) {
                    if (record.first >= cutoff_time) {
                        trend.push_back(record);
                    }
                }

                return trend;
            }

            void MemoryManager::checkMemoryPressure() {
                auto stats = getGlobalStatistics();
                double usage_ratio = static_cast<double>(stats.total_used_memory) / config_.max_total_memory;

                PressureLevel new_level;
                if (usage_ratio < 0.5) {
                    new_level = PressureLevel::LOW;
                } else if (usage_ratio < 0.7) {
                    new_level = PressureLevel::MODERATE;
                } else if (usage_ratio < config_.memory_pressure_threshold) {
                    new_level = PressureLevel::HIGH;
                } else {
                    new_level = PressureLevel::CRITICAL;
                }

                PressureLevel old_level = current_pressure_level_.exchange(new_level);

                if (new_level != old_level && new_level >= PressureLevel::HIGH) {
                    handleMemoryPressure(new_level);
                }
            }

            void MemoryManager::handleMemoryPressure(PressureLevel level) {
                switch (level) {
                case PressureLevel::HIGH:
                    // 触发部分垃圾回收
                    if (frame_allocator_) {
                        frame_allocator_->cleanup();
                    }
                    if (packet_recycler_) {
                        packet_recycler_->forceGarbageCollection();
                    }
                    break;

                case PressureLevel::CRITICAL:
                    // 全面垃圾回收
                    forceGarbageCollection();
                    break;

                default:
                    break;
                }

                // 触发回调通知
                if (pressure_callback_) {
                    auto stats = getGlobalStatistics();
                    std::string description;

                    switch (level) {
                    case PressureLevel::HIGH:
                        description = "High memory pressure detected";
                        break;
                    case PressureLevel::CRITICAL:
                        description = "Critical memory pressure - forced cleanup";
                        break;
                    default:
                        break;
                    }

                    PressureEvent event(level, stats.total_used_memory, config_.max_total_memory, description);
                    pressure_callback_(event);
                }
            }

            void MemoryManager::collectGlobalStatistics() {
                std::lock_guard<std::mutex> lock(stats_mutex_);

                global_stats_ = GlobalStatistics{};

                // 收集各组件统计并汇总
                if (memory_pool_) {
                    auto pool_stats = memory_pool_->getStatistics();
                    global_stats_.total_allocated_memory += pool_stats.total_allocated.load();
                    global_stats_.total_used_memory += pool_stats.current_usage.load();
                    global_stats_.peak_memory_usage = std::max(global_stats_.peak_memory_usage,
                                                               pool_stats.peak_usage.load());
                }

                if (frame_allocator_) {
                    auto frame_stats = frame_allocator_->getStatistics();
                    global_stats_.total_allocated_memory += frame_stats.total_allocated.load();
                    global_stats_.total_used_memory += frame_stats.total_memory_usage.load();
                    global_stats_.peak_memory_usage = std::max(global_stats_.peak_memory_usage,
                                                               frame_stats.peak_memory_usage.load());
                }

                if (packet_recycler_) {
                    auto packet_stats = packet_recycler_->getStatistics();
                    global_stats_.total_allocated_memory += packet_stats.total_allocated.load();
                    global_stats_.total_used_memory += packet_stats.current_memory_usage.load();
                    global_stats_.peak_memory_usage = std::max(global_stats_.peak_memory_usage,
                                                               packet_stats.peak_memory_usage.load());
                }

                // 计算效率指标
                if (global_stats_.peak_memory_usage > 0) {
                    global_stats_.overall_efficiency = static_cast<double>(global_stats_.total_used_memory) /
                                                       global_stats_.peak_memory_usage;
                }

                // 记录历史
                recordMemoryUsage();
            }

            void MemoryManager::recordMemoryUsage() {
                auto now = std::chrono::steady_clock::now();
                memory_history_.emplace_back(now, global_stats_.total_used_memory);

                // 保持历史记录在合理范围内（最近2小时）
                auto cutoff_time = now - std::chrono::hours(2);
                memory_history_.erase(
                    std::remove_if(memory_history_.begin(), memory_history_.end(),
                                   [cutoff_time](const auto& record) {
                                       return record.first < cutoff_time;
                                   }),
                    memory_history_.end());
            }

            void MemoryManager::startBackgroundThreads() {
                // 启动监控线程
                if (config_.enable_global_tracking) {
                    monitoring_running_.store(true);
                    monitoring_thread_ = std::thread(&MemoryManager::monitoringThread, this);
                }

                // 启动优化线程
                if (config_.enable_auto_optimization) {
                    optimization_running_.store(true);
                    optimization_thread_ = std::thread(&MemoryManager::optimizationThread, this);
                }
            }

            void MemoryManager::stopBackgroundThreads() {
                // 停止监控线程
                monitoring_running_.store(false);
                monitoring_cv_.notify_all();
                if (monitoring_thread_.joinable()) {
                    monitoring_thread_.join();
                }

                // 停止优化线程
                optimization_running_.store(false);
                optimization_cv_.notify_all();
                if (optimization_thread_.joinable()) {
                    optimization_thread_.join();
                }
            }

            void MemoryManager::monitoringThread() {
                std::unique_lock<std::mutex> lock(monitoring_mutex_);

                while (monitoring_running_.load() && !shutdown_.load()) {
                    if (monitoring_cv_.wait_for(lock, std::chrono::seconds(1)) == std::cv_status::timeout) {
                        // 每秒收集一次统计信息
                        collectGlobalStatistics();
                        checkMemoryPressure();

                        // 调用性能回调
                        if (performance_callback_) {
                            performance_callback_(global_stats_);
                        }
                    }
                }
            }

            void MemoryManager::optimizationThread() {
                std::unique_lock<std::mutex> lock(optimization_mutex_);

                while (optimization_running_.load() && !shutdown_.load()) {
                    if (optimization_cv_.wait_for(lock, std::chrono::milliseconds(config_.optimization_interval_ms))
                        == std::cv_status::timeout) {

                        // 执行优化
                        optimizeConfiguration();
                    }
                }
            }

            // 显式实例化模板方法
            template CacheManager<std::string, std::string>& MemoryManager::getCacheManager<std::string, std::string>();
            template CacheManager<int, std::vector<uint8_t>>& MemoryManager::getCacheManager<int, std::vector<uint8_t>>();ator::Config frame_config;

            switch (config_.strategy) {
            case Strategy::PERFORMANCE:
                frame_config.frames_per_pool = 32;
                frame_config.max_pools = 64;
                break;
            case Strategy::MEMORY_SAVING:
                frame_config.frames_per_pool = 8;
                frame_config.max_pools = 16;
                break;
            case Strategy::BALANCED:
            default:
                frame_config.frames_per_pool = 16;
                frame_config.max_pools = 32;
                break;
            }

            frame_config.enable_statistics = config_.enable_global_tracking;
            frame_allocator_ = std::make_unique<FrameAllocator>(frame_config);

            // 设置内存压力回调
            frame_allocator_->setMemoryPressureCallback([this](size_t current, size_t peak) {
                checkMemoryPressure();
            });
        }

        // 初始化包回收器
        if (config_.use_packet_recycler) {
            PacketRecycler::Config packet_config;

            switch (config_.strategy) {
            case Strategy::PERFORMANCE:
                packet_config.packets_per_pool = 64;
                packet_config.max_pools_per_category = 16;
                break;
            case Strategy::MEMORY_SAVING:
                packet_config.packets_per_pool = 16;
                packet_config.max_pools_per_category = 4;
                break;
            case Strategy::BALANCED:
            default:
                packet_config.packets_per_pool = 32;
                packet_config.max_pools_per_category = 8;
                break;
            }

            packet_config.enable_statistics = config_.enable_global_tracking;
            packet_config.max_total_memory = config_.max_total_memory / 4;  // 分配1/4给packet
            packet_recycler_ = std::make_unique<PacketRecycler>(packet_config);

            // 设置内存压力回调
            packet_recycler_->setMemoryPressureCallback([this](size_t current, size_t max_mem) {
                checkMemoryPressure();
            });
        }

        return true;

    } catch (const std::exception& e) {
        // 初始化失败，清理已创建的组件
        frame_allocator_.reset();
        packet_recycler_.reset();
        memory_pool_.reset();
        memory_tracker_.reset();
        return false;
    }
}

void MemoryManager::applyStrategy(Strategy strategy) {
    switch (strategy) {
    case Strategy::PERFORMANCE:
        // 性能优先：大缓存，多预分配
        config_.use_memory_pool = true;
        config_.use_object_pools = true;
        config_.use_frame_allocator = true;
        config_.use_packet_recycler = true;
        config_.use_cache_manager = true;
        break;

    case Strategy::MEMORY_SAVING:
        // 内存节约：小缓存，及时释放
        config_.use_memory_pool = true;
        config_.use_object_pools = false;
        config_.use_frame_allocator = false;
        config_.use_packet_recycler = false;
        config_.use_cache_manager = false;
        break;

    case Strategy::BALANCED:
        // 平衡模式：适中配置
        config_.use_memory_pool = true;
        config_.use_object_pools = true;
        config_.use_frame_allocator = true;
        config_.use_packet_recycler = true;
        config_.use_cache_manager = false;
        break;

    case Strategy::CUSTOM:
        // 自定义：保持用户配置
        break;
    }
}

void MemoryManager::applyScenarioConfig(ScenarioType scenario) {
    std::lock_guard<std::mutex> lock(config_mutex_);

    config_.scenario = scenario;

    switch (scenario) {
    case ScenarioType::SINGLE_STREAM:
        config_.max_total_memory = 256 * 1024 * 1024;  // 256MB
        config_.strategy = Strategy::MEMORY_SAVING;
        break;

    case ScenarioType::MULTI_STREAM:
        config_.max_total_memory = 1024 * 1024 * 1024; // 1GB
        config_.strategy = Strategy::BALANCED;
        break;

    case ScenarioType::REAL_TIME:
        config_.strategy = Strategy::PERFORMANCE;
        config_.enable_auto_optimization = false;  // 避免运行时调整
        break;

    case ScenarioType::BATCH_PROCESSING:
        config_.max_total_memory = 2048 * 1024 * 1024; // 2GB
        config_.strategy = Strategy::PERFORMANCE;
        break;

    case ScenarioType::LOW_LATENCY:
        config_.strategy = Strategy::PERFORMANCE;
        config_.optimization_interval_ms = 10000;  // 更频繁的优化
        break;

    case ScenarioType::HIGH_THROUGHPUT:
        config_.max_total_memory = 4096 * 1024 * 1024; // 4GB
        config_.strategy = Strategy::PERFORMANCE;
        break;
    }

    // 重新应用策略
    applyStrategy(config_.strategy);
}

MemoryPool& MemoryManager::getMemoryPool() {
    if (!memory_pool_) {
        throw std::runtime_error("MemoryPool not initialized");
    }
    return *memory_pool_;
}

MemoryTracker& MemoryManager::getMemoryTracker() {
    if (!memory_tracker_) {
        throw std::runtime_error("MemoryTracker not initialized");
    }
    return *memory_tracker_;
}

FrameAlloc

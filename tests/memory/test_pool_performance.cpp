#include "test_pool_performance.h"
#include "memory/memory_pool.h"
#include <vector>
#include <chrono>

void TestPoolPerformance::testPoolHitRate()
{
    MemoryPool::Config config;
    config.enable_statistics = true;
    pool = new MemoryPool(config);
    
    std::vector<void*> ptrs;
    for(int i = 0; i < 10; ++i) {
        ptrs.push_back(pool->allocate(512));
    }
    
    auto stats = pool->getStatistics();
    qDebug() << "Pool hit rate:" << (stats.getHitRate() * 100) << "%";
    qDebug() << "Pool hits:" << stats.pool_hit_count;
    qDebug() << "System allocs:" << stats.system_alloc_count;
    
    QVERIFY(stats.pool_hit_count > 0);
    QVERIFY(stats.getHitRate() > 0.0);
    
    for(void* ptr : ptrs) {
        pool->deallocate(ptr);
    }
    
    delete pool;
}

void TestPoolPerformance::testMemoryAlignment()
{
    MemoryPool::Config config;
    pool = new MemoryPool(config);
    
    void* ptr16 = pool->allocate(100, 16);
    void* ptr32 = pool->allocate(100, 32);
    void* ptr64 = pool->allocate(100, 64);
    
    QVERIFY(ptr16 != nullptr);
    QVERIFY(ptr32 != nullptr);
    QVERIFY(ptr64 != nullptr);
    
    QVERIFY(reinterpret_cast<uintptr_t>(ptr16) % 16 == 0);
    QVERIFY(reinterpret_cast<uintptr_t>(ptr32) % 32 == 0);
    QVERIFY(reinterpret_cast<uintptr_t>(ptr64) % 64 == 0);
    
    qDebug() << "16-byte aligned ptr:" << ptr16;
    qDebug() << "32-byte aligned ptr:" << ptr32;
    qDebug() << "64-byte aligned ptr:" << ptr64;
    
    pool->deallocate(ptr16);
    pool->deallocate(ptr32);
    pool->deallocate(ptr64);
    
    delete pool;
}

void TestPoolPerformance::testLargeAllocations()
{
    MemoryPool::Config config;
    pool = new MemoryPool(config);
    
    void* large_ptr = pool->allocate(2 * 1024 * 1024);
    QVERIFY(large_ptr != nullptr);
    
    auto stats = pool->getStatistics();
    qDebug() << "Large allocation - System allocs:" << stats.system_alloc_count;
    
    QVERIFY(stats.system_alloc_count > 0);
    
    pool->deallocate(large_ptr);
    delete pool;
}

void TestPoolPerformance::testReportGeneration()
{
    MemoryPool::Config config;
    pool = new MemoryPool(config);
    
    std::vector<void*> ptrs;
    ptrs.push_back(pool->allocate(512));
    ptrs.push_back(pool->allocate(32768));
    ptrs.push_back(pool->allocate(1048576));
    
    std::string report = pool->getReport();
    qDebug() << "Memory Pool Report:";
    qDebug() << report.c_str();
    
    QVERIFY(!report.empty());
    QVERIFY(report.find("Memory Pool Report") != std::string::npos);
    
    for(void* ptr : ptrs) {
        pool->deallocate(ptr);
    }
    
    delete pool;
}

void TestPoolPerformance::benchmarkPerformance()
{
    MemoryPool::Config config;
    pool = new MemoryPool(config);
    
    const int iterations = 1000;
    std::vector<void*> ptrs;
    ptrs.reserve(iterations);
    
    auto start = std::chrono::high_resolution_clock::now();
    
    for(int i = 0; i < iterations; ++i) {
        ptrs.push_back(pool->allocate(64 + (i % 256)));
    }
    
    auto mid = std::chrono::high_resolution_clock::now();
    
    for(void* ptr : ptrs) {
        pool->deallocate(ptr);
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    
    auto alloc_duration = std::chrono::duration_cast<std::chrono::microseconds>(mid - start);
    auto dealloc_duration = std::chrono::duration_cast<std::chrono::microseconds>(end - mid);
    
    qDebug() << "Benchmark Results:";
    qDebug() << "  Allocations:" << iterations << "times in" << alloc_duration.count() << "μs";
    qDebug() << "  Deallocations:" << iterations << "times in" << dealloc_duration.count() << "μs";
    qDebug() << "  Avg allocation time:" << (alloc_duration.count() / iterations) << "μs";
    qDebug() << "  Avg deallocation time:" << (dealloc_duration.count() / iterations) << "μs";
    
    auto stats = pool->getStatistics();
    qDebug() << "  Final hit rate:" << (stats.getHitRate() * 100) << "%";
    
    delete pool;
}

#include "test_pool_performance.moc"
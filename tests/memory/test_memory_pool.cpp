#include "test_memory_pool.h"
#include "memory/memory_pool.h"
#include <vector>

void TestMemoryPool::testBasicAllocation()
{
    MemoryPool::Config config;
    pool = new MemoryPool(config);
    
    void* ptr = pool->allocate(64);
    QVERIFY(ptr != nullptr);
    
    pool->deallocate(ptr);
    delete pool;
}

void TestMemoryPool::testMultipleAllocations()
{
    MemoryPool::Config config;
    pool = new MemoryPool(config);
    
    std::vector<void*> ptrs;
    
    for (int i = 1; i <= 5; ++i) {
        void* ptr = pool->allocate(64 * i);
        QVERIFY(ptr != nullptr);
        ptrs.push_back(ptr);
    }
    
    for (void* ptr : ptrs) {
        pool->deallocate(ptr);
    }
    
    delete pool;
}

void TestMemoryPool::testDeallocation()
{
    MemoryPool::Config config;
    pool = new MemoryPool(config);
    
    void* ptr1 = pool->allocate(1024);
    void* ptr2 = pool->allocate(2048);
    
    QVERIFY(ptr1 != nullptr);
    QVERIFY(ptr2 != nullptr);
    QVERIFY(ptr1 != ptr2);
    
    pool->deallocate(ptr1);
    pool->deallocate(ptr2);
    
    delete pool;
}

void TestMemoryPool::testStatistics()
{
    MemoryPool::Config config;
    config.enable_statistics = true;
    pool = new MemoryPool(config);
    
    auto stats_before = pool->getStatistics();
    
    void* ptr = pool->allocate(512);
    QVERIFY(ptr != nullptr);
    
    auto stats_after = pool->getStatistics();
    QVERIFY(stats_after.allocation_count > stats_before.allocation_count);
    
    pool->deallocate(ptr);
    delete pool;
}

#include "test_memory_pool.moc"
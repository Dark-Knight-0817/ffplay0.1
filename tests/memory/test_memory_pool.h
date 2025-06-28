#ifndef TEST_MEMORY_POOL_H
#define TEST_MEMORY_POOL_H

#include <QtTest>
#include <QObject>

// 前向声明
class MemoryPool;

class TestMemoryPool : public QObject
{
    Q_OBJECT

private slots:
    void testBasicAllocation();
    void testMultipleAllocations();
    void testDeallocation();
    void testStatistics();

private:
    MemoryPool* pool;
};

#endif // TEST_MEMORY_POOL_H
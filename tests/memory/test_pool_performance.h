#ifndef TEST_POOL_PERFORMANCE_H
#define TEST_POOL_PERFORMANCE_H

#include <QtTest>
#include <QObject>

// 前向声明
class MemoryPool;

class TestPoolPerformance : public QObject
{
    Q_OBJECT

private slots:
    void testPoolHitRate();
    void testMemoryAlignment();
    void testLargeAllocations();
    void testReportGeneration();
    void benchmarkPerformance();

private:
    MemoryPool* pool;
};

#endif // TEST_POOL_PERFORMANCE_H
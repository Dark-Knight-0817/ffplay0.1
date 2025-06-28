#include <QtTest>
#include <QCoreApplication>
#include "memory/test_memory_pool.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    int result = 0;
    
    qDebug() << "开始运行内存池测试";
    
    TestMemoryPool test;
    result += QTest::qExec(&test, argc, argv);
    
    qDebug() << "测试完成，结果代码:" << result;
    
    return result;
}
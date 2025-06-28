#include <QtTest>
#include <QCoreApplication>

// 包含测试类头文件（不是cpp文件）
#include "memory/test_memory_pool.h"
#include "memory/test_pool_performance.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    int result = 0;
    
    qDebug() << "🎯 FFplay0.1 内存池测试套件 (头文件分离版)";
    qDebug() << "==============================";
    
    // 基础功能测试
    qDebug() << "\n📋 1. 基础功能测试";
    {
        TestMemoryPool basicTest;
        int basicResult = QTest::qExec(&basicTest, argc, argv);
        result += basicResult;
        
        if (basicResult == 0) {
            qDebug() << "   ✅ 基础功能全部通过";
        } else {
            qDebug() << "   ❌ 基础功能有" << basicResult << "个失败";
        }
    }
    
    // 性能测试
    qDebug() << "\n🚀 2. 性能和高级功能测试";
    {
        TestPoolPerformance perfTest;
        int perfResult = QTest::qExec(&perfTest, argc, argv);
        result += perfResult;
        
        if (perfResult == 0) {
            qDebug() << "   ✅ 性能测试全部通过";
        } else {
            qDebug() << "   ❌ 性能测试有" << perfResult << "个失败";
        }
    }
    
    // 总结
    qDebug() << "\n==============================";
    if (result == 0) {
        qDebug() << "🎉 所有测试通过！内存池实现完美！";
        qDebug() << "📊 使用头文件分离的标准C++项目结构";
    } else {
        qDebug() << "❌ 总共有" << result << "个测试失败";
    }
    qDebug() << "==============================";
    
    return result;
}
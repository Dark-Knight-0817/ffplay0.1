#include <QtTest>
#include <QCoreApplication>

// 包含测试类头文件（不是cpp文件）
#include "memory/test_memory_pool.h"
#include "memory/test_pool_performance.h"

#ifdef FFMPEG_AVAILABLE
#include "media/allocator/test_ffmpeg_frame_allocator.h"
#endif

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    int result = 0;
    
    qDebug() << "🎯 FFplay0.1 测试套件 (头文件分离版)";
    qDebug() << "==========================================";
    
    // 检查命令行参数，支持单独运行某个模块的测试
    QString filter;
    if (argc > 1) {
        filter = QString(argv[1]);
    }
    
    // 1. 内存池测试
    if (filter.isEmpty() || filter == "memory") {
        qDebug() << "\n📋 1. 内存池模块测试";
        qDebug() << "----------------------------------------";
        
        // 基础功能测试
        qDebug() << "\n🔧 1.1 基础功能测试";
        {
            TestMemoryPool basicTest;
            int basicResult = QTest::qExec(&basicTest, argc, argv);
            result += basicResult;
            
            if (basicResult == 0) {
                qDebug() << "   ✅ 内存池基础功能全部通过";
            } else {
                qDebug() << "   ❌ 内存池基础功能有" << basicResult << "个失败";
            }
        }
        
        // 性能测试
        qDebug() << "\n🚀 1.2 性能测试";
        {
            TestPoolPerformance perfTest;
            int perfResult = QTest::qExec(&perfTest, argc, argv);
            result += perfResult;
            
            if (perfResult == 0) {
                qDebug() << "   ✅ 内存池性能测试全部通过";
            } else {
                qDebug() << "   ❌ 内存池性能测试有" << perfResult << "个失败";
            }
        }
    }
    
#ifdef FFMPEG_AVAILABLE
    // 2. Frame Allocator测试
    if (filter.isEmpty() || filter == "media") {
        qDebug() << "\n🎬 2. Frame Allocator模块测试";
        qDebug() << "----------------------------------------";
        
        qDebug() << "\n🎞️ 2.1 FFmpeg Frame Allocator测试";
        {
            TestFrameAllocator frameTest;
            int frameResult = QTest::qExec(&frameTest, argc, argv);
            result += frameResult;
            
            if (frameResult == 0) {
                qDebug() << "   ✅ FFmpeg Frame Allocator全部通过";
            } else {
                qDebug() << "   ❌ FFmpeg Frame Allocator有" << frameResult << "个失败";
            }
        }
    }
#else
    if (filter.isEmpty() || filter == "media") {
        qDebug() << "\n⚠️  2. Frame Allocator模块测试";
        qDebug() << "----------------------------------------";
        qDebug() << "FFmpeg不可用，跳过Frame Allocator测试";
        qDebug() << "要启用此测试，请确保：";
        qDebug() << "1. 安装FFmpeg开发库";
        qDebug() << "2. 在CMakeLists.txt中正确配置FFmpeg路径";
        qDebug() << "3. 定义FFMPEG_AVAILABLE宏";
    }
#endif
    
    // 总结
    qDebug() << "\n==========================================";
    qDebug() << "🏁 测试总结";
    qDebug() << "==========================================";
    
    if (result == 0) {
        qDebug() << "🎉 所有测试通过！实现完美！";
        qDebug() << "📊 使用头文件分离的标准C++项目结构";
        
#ifdef FFMPEG_AVAILABLE
        qDebug() << "🎬 FFmpeg Frame Allocator: ✅ 启用并测试通过";
#else
        qDebug() << "🎬 FFmpeg Frame Allocator: ⚠️  未启用";
#endif
        
    } else {
        qDebug() << "❌ 总共有" << result << "个测试失败";
        qDebug() << "";
        qDebug() << "💡 调试建议：";
        qDebug() << "   - 检查源文件是否存在";
        qDebug() << "   - 确认包含路径正确";
        qDebug() << "   - 验证FFmpeg库链接";
    }
    
    qDebug() << "==========================================";
    qDebug() << "";
    qDebug() << "📖 使用说明：";
    qDebug() << "   ./run_tests           # 运行所有测试";
    qDebug() << "   ./run_tests memory    # 只运行内存池测试";
    qDebug() << "   ./run_tests media     # 只运行Frame Allocator测试";
    
    return result;
}
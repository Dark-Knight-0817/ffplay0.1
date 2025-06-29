// test_ffmpeg_frame_allocator.h
#ifndef TEST_FFMPEG_FRAME_ALLOCATOR_H
#define TEST_FFMPEG_FRAME_ALLOCATOR_H

#include <QtTest>
#include <QObject>

// 包含必要的头文件
#include "media/allocator/frame_allocator_base.h"
#include "media/allocator/frame_allocator_factory.h"

#ifdef FFMPEG_AVAILABLE
    #include "media/allocator/ffmpeg_allocator/ffmpeg_frame_allocator.h"
#endif

/**
 * @brief FrameAllocator 测试类
 * 
 * 测试帧分配器的各项功能：
 * - 工厂模式创建
 * - FFmpeg后端功能  
 * - 池重用机制
 * - 统计信息
 * - 全局单例管理
 */
class TestFrameAllocator : public QObject
{
    Q_OBJECT

private slots:
    // 测试框架方法
    void initTestCase();    // 整个测试开始前
    void cleanupTestCase(); // 整个测试结束后
    void init();           // 每个测试开始前
    void cleanup();        // 每个测试结束后

    // 基础功能测试
    void testFactoryCreation();      // 工厂创建功能
    void testBackendDetection();     // 后端检测
    void testFrameSpecHash();        // FrameSpec哈希
    void testAllocatedFrameBasics(); // 基础帧分配

    // FFmpeg特定测试 (仅在有FFmpeg时编译)
#ifdef FFMPEG_AVAILABLE
    void testFFmpegAllocatorCreation(); // FFmpeg分配器创建
    void testFFmpegFrameAllocation();   // FFmpeg帧分配
    void testFFmpegPoolReuse();         // FFmpeg池重用
    void testFFmpegStatistics();        // FFmpeg统计
#endif

    // 高级功能测试
    void testGlobalAllocatorSingleton(); // 全局单例
    void testBackendSwitching();         // 后端切换
    void testCustomBackendRegistration(); // 自定义后端注册

private:
    // 测试辅助方法 - 现在有了完整的类型定义
    void validateFrameData(const media::FrameData* frame, int width, int height);
    void validateStatistics(const media::Statistics& stats);
};

#endif // TEST_FFMPEG_FRAME_ALLOCATOR_H
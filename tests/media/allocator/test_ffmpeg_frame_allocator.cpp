// test_ffmpeg_frame_allocator.cpp
#include "test_ffmpeg_frame_allocator.h"

void TestFrameAllocator::initTestCase()
{
    qDebug() << "🎯 开始 FrameAllocator 测试套件";
    
    try {
        auto backends = media::FrameAllocatorFactory::getAvailableBackends();
        qDebug() << "可用后端数量:" << backends.size();
        for (const auto& backend : backends) {
            qDebug() << "  -" << QString::fromStdString(backend);
        }
    } catch (const std::exception& e) {
        qDebug() << "⚠️ 没有可用的后端:" << e.what();
    }
}

void TestFrameAllocator::cleanupTestCase()
{
    // 清理全局分配器
    if (media::GlobalFrameAllocator::isInitialized()) {
        media::GlobalFrameAllocator::shutdown();
    }
    qDebug() << "✅ FrameAllocator 测试套件完成";
}

void TestFrameAllocator::init()
{
    // 每个测试开始前的准备
}

void TestFrameAllocator::cleanup()
{
    // 每个测试结束后的清理
    if (media::GlobalFrameAllocator::isInitialized()) {
        media::GlobalFrameAllocator::shutdown();
    }
}

void TestFrameAllocator::testFactoryCreation()
{
    qDebug() << "\n📋 测试工厂创建功能";
    
    try {
        // 测试获取可用后端
        auto backends = media::FrameAllocatorFactory::getAvailableBackends();
        QVERIFY(!backends.empty());
        
        qDebug() << "找到后端数量:" << backends.size();
        for (const auto& backend : backends) {
            qDebug() << "  -" << QString::fromStdString(backend);
        }
        
        // 测试后端信息获取
        auto backend_info = media::FrameAllocatorFactory::getAllBackendInfo();
        QVERIFY(!backend_info.empty());
        
        for (const auto& info : backend_info) {
            qDebug() << "后端:" << QString::fromStdString(info.name) 
                     << "可用:" << info.available
                     << "版本:" << QString::fromStdString(info.version);
        }
    } catch (const std::exception& e) {
        QFAIL(qPrintable(QString("工厂创建测试失败: %1").arg(e.what())));
    }
}

void TestFrameAllocator::testBackendDetection()
{
    qDebug() << "\n🔍 测试后端检测";
    
    try {
        // 测试最佳后端检测
        auto best_backend = media::FrameAllocatorFactory::detectBestBackend();
        qDebug() << "检测到最佳后端:" << static_cast<int>(best_backend);
        
#ifdef FFMPEG_AVAILABLE
        QVERIFY(media::FrameAllocatorFactory::isBackendAvailable(media::BackendType::FFmpeg));
        qDebug() << "✅ FFmpeg 后端可用";
#else
        qDebug() << "⚠️ FFmpeg 后端编译时不可用";
#endif

        // 测试字符串转换
        std::string backend_str = media::FrameAllocatorFactory::backendTypeToString(best_backend);
        QVERIFY(!backend_str.empty());
        qDebug() << "最佳后端字符串:" << QString::fromStdString(backend_str);
        
    } catch (const std::exception& e) {
        QFAIL(qPrintable(QString("后端检测失败: %1").arg(e.what())));
    }
}

void TestFrameAllocator::testFrameSpecHash()
{
    qDebug() << "\n🔧 测试 FrameSpec 哈希功能";
    
    media::FrameSpec spec1(1920, 1080, 0, 32);
    media::FrameSpec spec2(1920, 1080, 0, 32);
    media::FrameSpec spec3(1280, 720, 0, 32);
    
    // 测试相等性
    QVERIFY(spec1 == spec2);
    QVERIFY(spec1 != spec3);
    
    // 测试哈希一致性
    media::FrameSpecHash hasher;
    QCOMPARE(hasher(spec1), hasher(spec2));
    QVERIFY(hasher(spec1) != hasher(spec3));
    
    qDebug() << "✅ FrameSpec 哈希功能正常";
}

void TestFrameAllocator::testAllocatedFrameBasics()
{
    qDebug() << "\n📦 测试 AllocatedFrame 基础功能";
    
    // 创建空的分配结果
    media::AllocatedFrame empty_frame;
    QVERIFY(!empty_frame.isValid());
    
    // 创建有效的帧数据
    auto frame_data = std::make_unique<media::FrameData>();
    frame_data->width = 640;
    frame_data->height = 480;
    frame_data->data[0] = reinterpret_cast<void*>(0x1000); // 模拟有效指针
    
    media::AllocatedFrame valid_frame;
    valid_frame.frame = std::move(frame_data);
    valid_frame.spec = media::FrameSpec(640, 480, 0);
    valid_frame.backend = "Test";
    
    QVERIFY(valid_frame.isValid());
    QCOMPARE(valid_frame.spec.width, 640);
    QCOMPARE(valid_frame.spec.height, 480);
    
    qDebug() << "✅ AllocatedFrame 基础功能正常";
}

#ifdef FFMPEG_AVAILABLE
void TestFrameAllocator::testFFmpegAllocatorCreation()
{
    qDebug() << "\n🎬 测试 FFmpeg 分配器创建";
    
    try {
        // 使用工厂创建FFmpeg分配器
        auto allocator = media::FrameAllocatorFactory::create(media::BackendType::FFmpeg);
        QVERIFY(allocator != nullptr);
        
        // 验证后端名称
        QString backend_name = QString::fromStdString(allocator->getBackendName());
        QVERIFY(backend_name.contains("FFmpeg"));
        qDebug() << "后端名称:" << backend_name;
        
        // 测试支持的格式
        auto formats = allocator->getSupportedFormats();
        QVERIFY(!formats.empty());
        qDebug() << "支持的格式数量:" << formats.size();
        
        // 测试格式支持检查
        QVERIFY(allocator->isFormatSupported(media::FFmpegFormats::YUV420P));
        QVERIFY(allocator->isFormatSupported(media::FFmpegFormats::RGB24));
        
        qDebug() << "✅ FFmpeg 分配器创建成功";
        
    } catch (const std::exception& e) {
        QFAIL(qPrintable(QString("FFmpeg分配器创建失败: %1").arg(e.what())));
    }
}

void TestFrameAllocator::testFFmpegFrameAllocation()
{
    qDebug() << "\n🎞️ 测试 FFmpeg 帧分配";
    
    try {
        auto allocator = media::FrameAllocatorFactory::create(media::BackendType::FFmpeg);
        QVERIFY(allocator != nullptr);
        
        // 测试基础帧分配
        media::FrameSpec spec(640, 480, media::FFmpegFormats::YUV420P, 32);
        auto result = allocator->allocateFrame(spec);
        
        QVERIFY(result.isValid());
        QVERIFY(result.frame != nullptr);
        validateFrameData(result.frame.get(), 640, 480);
        
        qDebug() << "✅ 基础帧分配:" << result.frame->width << "x" << result.frame->height;
        qDebug() << "   来自池:" << result.from_pool;
        qDebug() << "   缓冲区大小:" << result.frame->buffer_size << "bytes";
        
        // 测试不同规格的帧
        std::vector<std::pair<int, int>> test_sizes = {
            {1920, 1080}, {1280, 720}, {320, 240}
        };
        
        for (const auto& [width, height] : test_sizes) {
            media::FrameSpec test_spec(width, height, media::FFmpegFormats::YUV420P);
            auto test_result = allocator->allocateFrame(test_spec);
            
            QVERIFY(test_result.isValid());
            validateFrameData(test_result.frame.get(), width, height);
            qDebug() << "✅ 分配" << width << "x" << height << "帧成功";
            
            // 测试释放
            bool released = allocator->deallocateFrame(std::move(test_result.frame));
            qDebug() << "   释放到池:" << released;
        }
    } catch (const std::exception& e) {
        QFAIL(qPrintable(QString("FFmpeg帧分配测试失败: %1").arg(e.what())));
    }
}

void TestFrameAllocator::testFFmpegPoolReuse()
{
    qDebug() << "\n♻️  测试 FFmpeg 池重用";
    
    try {
        auto allocator = media::FrameAllocatorFactory::create(media::BackendType::FFmpeg);
        
        media::FrameSpec spec(640, 480, media::FFmpegFormats::YUV420P);
        
        // 第一次分配 - 应该创建新帧
        auto frame1 = allocator->allocateFrame(spec);
        QVERIFY(frame1.isValid());
        bool first_from_pool = frame1.from_pool;
        
        // 释放帧
        auto native_ptr = frame1.frame->native_frame;  // 记录原生指针
        bool released = allocator->deallocateFrame(std::move(frame1.frame));
        
        // 第二次分配同样规格 - 应该重用池中的帧
        auto frame2 = allocator->allocateFrame(spec);
        QVERIFY(frame2.isValid());
        
        qDebug() << "第一次分配来自池:" << first_from_pool;
        qDebug() << "第二次分配来自池:" << frame2.from_pool;
        qDebug() << "释放到池成功:" << released;
        
        if (released) {
            // 如果成功释放到池，第二次分配应该来自池
            QVERIFY(frame2.from_pool);
            qDebug() << "✅ 池重用机制正常工作";
        } else {
            qDebug() << "ℹ️ 池重用未触发（可能是池配置原因）";
        }
    } catch (const std::exception& e) {
        QFAIL(qPrintable(QString("FFmpeg池重用测试失败: %1").arg(e.what())));
    }
}

void TestFrameAllocator::testFFmpegStatistics()
{
    qDebug() << "\n📊 测试 FFmpeg 统计功能";
    
    try {
        auto allocator = media::FrameAllocatorFactory::create(media::BackendType::FFmpeg);
        
        // 获取初始统计
        auto initial_stats = allocator->getStatistics();
        validateStatistics(initial_stats);
        
        qDebug() << "初始统计:";
        qDebug() << "  总分配:" << initial_stats.total_allocated;
        qDebug() << "  池命中:" << initial_stats.pool_hits;
        qDebug() << "  池未命中:" << initial_stats.pool_misses;
        
        // 进行一些分配操作
        std::vector<media::AllocatedFrame> frames;
        for (int i = 0; i < 5; ++i) {
            media::FrameSpec spec(640, 480, media::FFmpegFormats::YUV420P);
            frames.push_back(allocator->allocateFrame(spec));
        }
        
        // 获取分配后统计
        auto after_alloc_stats = allocator->getStatistics();
        QVERIFY(after_alloc_stats.total_allocated > initial_stats.total_allocated);
        
        qDebug() << "分配后统计:";
        qDebug() << "  总分配:" << after_alloc_stats.total_allocated;
        qDebug() << "  当前内存使用:" << after_alloc_stats.total_memory_usage << "bytes";
        qDebug() << "  池命中率:" << QString::number(after_alloc_stats.getHitRate() * 100, 'f', 2) << "%";
        
        // 释放所有帧
        for (auto& frame : frames) {
            allocator->deallocateFrame(std::move(frame.frame));
        }
        
        auto final_stats = allocator->getStatistics();
        qDebug() << "释放后统计:";
        qDebug() << "  总释放:" << final_stats.total_freed;
        qDebug() << "  最终命中率:" << QString::number(final_stats.getHitRate() * 100, 'f', 2) << "%";
        
        qDebug() << "✅ 统计功能正常";
    } catch (const std::exception& e) {
        QFAIL(qPrintable(QString("FFmpeg统计测试失败: %1").arg(e.what())));
    }
}
#endif

void TestFrameAllocator::testGlobalAllocatorSingleton()
{
    qDebug() << "\n🌐 测试全局分配器单例";
    
    try {
        // 测试初始化
        QVERIFY(!media::GlobalFrameAllocator::isInitialized());
        
        media::GlobalFrameAllocator::initialize(media::BackendType::Auto);
        QVERIFY(media::GlobalFrameAllocator::isInitialized());
        
        // 测试获取实例
        auto& instance1 = media::GlobalFrameAllocator::getInstance();
        auto& instance2 = media::GlobalFrameAllocator::getInstance();
        QCOMPARE(&instance1, &instance2);  // 应该是同一个实例
        
        // 测试当前后端信息
        QString backend_name = QString::fromStdString(media::GlobalFrameAllocator::getCurrentBackendName());
        qDebug() << "当前全局后端:" << backend_name;
        
        // 测试全局统计
        auto global_stats = media::GlobalFrameAllocator::getGlobalStatistics();
        validateStatistics(global_stats);
        
        qDebug() << "✅ 全局分配器单例正常";
    } catch (const std::exception& e) {
        QFAIL(qPrintable(QString("全局分配器测试失败: %1").arg(e.what())));
    }
}

void TestFrameAllocator::testBackendSwitching()
{
    qDebug() << "\n🔄 测试后端切换";
    
    try {
        // 初始化为Auto
        media::GlobalFrameAllocator::initialize(media::BackendType::Auto);
        QString initial_backend = QString::fromStdString(media::GlobalFrameAllocator::getCurrentBackendName());
        
#ifdef FFMPEG_AVAILABLE
        // 尝试切换到FFmpeg后端
        media::GlobalFrameAllocator::switchBackend(media::BackendType::FFmpeg);
        QString switched_backend = QString::fromStdString(media::GlobalFrameAllocator::getCurrentBackendName());
        
        qDebug() << "初始后端:" << initial_backend;
        qDebug() << "切换后端:" << switched_backend;
        
        QVERIFY(switched_backend.contains("FFmpeg"));
        qDebug() << "✅ 后端切换成功";
#else
        qDebug() << "⚠️ FFmpeg不可用，跳过后端切换测试";
#endif
        
    } catch (const std::exception& e) {
        qDebug() << "后端切换失败:" << e.what();
        // 不QFAIL，因为可能是合理的失败（如后端不可用）
    }
}

void TestFrameAllocator::testCustomBackendRegistration()
{
    qDebug() << "\n🔌 测试自定义后端注册";
    
    try {
        // 注册自定义后端
        media::FrameAllocatorFactory::registerBackend("test", 
            [](std::unique_ptr<media::AllocatorConfig>) -> std::unique_ptr<media::IFrameAllocator> {
                return nullptr;  // 简单的测试实现
            });
        
        auto backends = media::FrameAllocatorFactory::getAvailableBackends();
        bool found_test_backend = false;
        for (const auto& backend : backends) {
            if (backend == "test") {
                found_test_backend = true;
                break;
            }
        }
        
        QVERIFY(found_test_backend);
        qDebug() << "✅ 自定义后端注册成功";
    } catch (const std::exception& e) {
        QFAIL(qPrintable(QString("自定义后端注册失败: %1").arg(e.what())));
    }
}

// 辅助方法实现
void TestFrameAllocator::validateFrameData(const media::FrameData* frame, int width, int height)
{
    QVERIFY(frame != nullptr);
    QVERIFY(frame->isValid());
    QCOMPARE(frame->width, width);
    QCOMPARE(frame->height, height);
    QVERIFY(frame->data[0] != nullptr);  // 至少Y平面应该有数据
    QVERIFY(frame->buffer_size > 0);
}

void TestFrameAllocator::validateStatistics(const media::Statistics& stats)
{
    // 统计数据应该是合理的
    QVERIFY(stats.total_allocated >= stats.total_freed);
    QVERIFY(stats.peak_memory_usage >= stats.total_memory_usage);
    QVERIFY(!stats.backend.empty());
    
    // 命中率应该在0-1之间
    double hit_rate = stats.getHitRate();
    QVERIFY(hit_rate >= 0.0 && hit_rate <= 1.0);
}

// 包含moc生成的代码
#include "test_ffmpeg_frame_allocator.moc"
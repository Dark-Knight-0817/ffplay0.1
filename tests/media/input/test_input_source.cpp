#include "test_input_source.h"

#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QDebug>
#include <QElapsedTimer>

TestInputSource::TestInputSource()
{
    // 构造函数
}

void TestInputSource::initTestCase()
{
    qDebug() << "🎬 开始输入源模块测试";
    qDebug() << "==========================================";
    
    // 创建测试文件
    test_file_path_ = createTestFile();
    QVERIFY(!test_file_path_.isEmpty());
    
    qDebug() << "✅ 测试环境准备完成";
    qDebug() << "   测试文件:" << test_file_path_;
}

void TestInputSource::cleanupTestCase()
{
    // 清理测试文件
    if (!test_file_path_.isEmpty()) {
        removeTestFile(test_file_path_);
    }
    
    qDebug() << "✅ 测试环境清理完成";
    qDebug() << "==========================================";
}

void TestInputSource::init()
{
    // 每个测试前的准备
}

void TestInputSource::cleanup()
{
    // 每个测试后的清理
}

void TestInputSource::testFactoryTypeDetection()
{
    qDebug() << "\n🔍 测试工厂类型检测";
    
    // 测试各种URL类型的检测
    QCOMPARE(media::InputSourceFactory::detectType("test.mp4"), media::InputSourceType::LocalFile);
    QCOMPARE(media::InputSourceFactory::detectType("video.avi"), media::InputSourceType::LocalFile);
    QCOMPARE(media::InputSourceFactory::detectType("rtsp://192.168.1.100:554/stream"), media::InputSourceType::RTSP);
    QCOMPARE(media::InputSourceFactory::detectType("http://example.com/stream.m3u8"), media::InputSourceType::HTTP);
    QCOMPARE(media::InputSourceFactory::detectType("udp://239.255.255.250:1234"), media::InputSourceType::UDP);
    QCOMPARE(media::InputSourceFactory::detectType("/path/to/file.mkv"), media::InputSourceType::LocalFile);
    
    qDebug() << "   ✓ 类型检测测试通过";
}

void TestInputSource::testFactoryCreation()
{
    qDebug() << "\n🏭 测试工厂创建";
    
    // 测试RTSP源创建
    auto rtsp_source = media::InputSourceFactory::create("rtsp://test.example.com/stream");
    QVERIFY(rtsp_source != nullptr);
    qDebug() << "   ✓ RTSP源创建成功";
    
    // 测试文件源创建
    auto file_source = media::InputSourceFactory::create("test.mp4");
    QVERIFY(file_source != nullptr);
    qDebug() << "   ✓ 文件源创建成功";
    
    // 测试默认行为
    auto default_source = media::InputSourceFactory::create("unknown://test");
    QVERIFY(default_source != nullptr); // 应该创建FileInput作为默认
    qDebug() << "   ✓ 默认类型处理正确";
}

void TestInputSource::testFileInputBasic() {
    qDebug() << "\n📁 测试文件输入基础功能";
    
    auto file_input = std::make_unique<media::FileInput>();
    
    // 1. 测试初始状态
    QCOMPARE(file_input->getState(), media::InputSourceState::Closed);
    qDebug() << "   ✓ 初始状态正确";
    
    // 2. 测试错误处理
    qDebug() << "   🔍 开始错误处理测试...";
    std::string non_existent_file = "/tmp/absolutely_non_existent_video_12345.mp4";
    
    bool success = file_input->open(non_existent_file);
    QVERIFY(!success);
    QCOMPARE(file_input->getState(), media::InputSourceState::Error);
    qDebug() << "   ✓ 错误处理测试通过:" << QString::fromStdString(file_input->getLastError());
    
    // 3. 测试关闭
    file_input->close();
    QCOMPARE(file_input->getState(), media::InputSourceState::Closed);
    qDebug() << "   ✓ 关闭功能测试通过";
    
    // 4. 测试文件打开
    if (!test_file_path_.isEmpty()) {
        qDebug() << "   🎯 测试文件:" << test_file_path_;
        
        // 检查是否是真实的视频文件
        bool is_real_video = test_file_path_.endsWith(".mp4") || 
                           test_file_path_.endsWith(".avi") || 
                           test_file_path_.endsWith(".mkv") ||
                           test_file_path_.endsWith(".mov");
        
        std::string file_path = test_file_path_.toStdString();
        success = file_input->open(file_path);
        
        if (success) {
            qDebug() << "   ✅ 文件打开成功！";
            QCOMPARE(file_input->getState(), media::InputSourceState::Opened);
            
            auto info = file_input->getSourceInfo();
            qDebug() << "   📊 格式:" << QString::fromStdString(info.format_name);
            qDebug() << "   📊 时长:" << info.duration / 1000000.0 << "秒";
            qDebug() << "   📊 码率:" << info.bit_rate;
            
            file_input->close();
            qDebug() << "   ✓ 文件关闭成功";
            
        } else {
            if (is_real_video) {
                qDebug() << "   ❌ 真实视频文件打开失败:" << QString::fromStdString(file_input->getLastError());
                // 真实视频文件打开失败可能是问题
            } else {
                qDebug() << "   ⚠️  测试数据文件打开失败（这是正常的）:" << QString::fromStdString(file_input->getLastError());
                // .dat 文件打开失败是正常的，它不是真的视频文件
            }
        }
    } else {
        qDebug() << "   ⚠️  无测试文件";
    }
    
    qDebug() << "   🎉 文件输入基础功能测试完成";
}

void TestInputSource::testFileInputError()
{
    qDebug() << "\n❌ 测试文件输入错误处理";
    
    media::FileInput file_input;
    
    // 测试打开不存在的文件
    bool opened = file_input.open("definitely_nonexistent_file_12345.mp4");
    QVERIFY(!opened);
    QCOMPARE(file_input.getState(), media::InputSourceState::Error);
    
    QString error = QString::fromStdString(file_input.getLastError());
    QVERIFY(!error.isEmpty());
    qDebug() << "   ✓ 正确处理文件不存在错误:" << error;
    
    // 测试重复打开（先打开一个有效文件）
    if (file_input.open(test_file_path_.toStdString())) {
        bool second_open = file_input.open(test_file_path_.toStdString());
        QVERIFY(!second_open); // 应该失败
        qDebug() << "   ✓ 重复打开正确被拒绝";
        file_input.close();
    }
}

void TestInputSource::testRTSPInputBasic()
{
    qDebug() << "\n📡 测试RTSP输入基础功能";
    
    media::RTSPInput rtsp_input;
    
    // 测试初始状态
    QCOMPARE(rtsp_input.getState(), media::InputSourceState::Closed);
    QVERIFY(!rtsp_input.isSeekable()); // RTSP不支持seek
    
    // 测试seek失败
    bool seek_result = rtsp_input.seek(1000000);
    QVERIFY(!seek_result); // 应该失败
    qDebug() << "   ✓ RTSP正确拒绝seek操作";
    
    // 测试连接无效地址（快速失败）
    QString invalid_url = "rtsp://192.168.255.255:554/nonexistent";
    
    QElapsedTimer timer;
    timer.start();
    
    bool opened = rtsp_input.open(invalid_url.toStdString());
    qint64 elapsed = timer.elapsed();
    
    QVERIFY(!opened);
    QCOMPARE(rtsp_input.getState(), media::InputSourceState::Error);
    
    QString error = QString::fromStdString(rtsp_input.getLastError());
    QVERIFY(!error.isEmpty());
    
    qDebug() << QString("   ✓ RTSP连接失败正确处理，耗时 %1ms").arg(elapsed);
    qDebug() << "   ✓ 错误信息:" << error;
}

void TestInputSource::testErrorHandling()
{
    qDebug() << "\n❗ 测试错误处理";
    
    // 测试各种错误情况
    media::FileInput file_input;
    
    // 未打开状态下的操作
    QVERIFY(!file_input.seek(1000));
    qDebug() << "   ✓ 未打开状态下的操作正确失败";
    
    // 获取错误信息
    QString error_msg = QString::fromStdString(file_input.getLastError());
    qDebug() << "   ✓ 错误信息获取正常";
    
    // 测试关闭未打开的源
    file_input.close(); // 应该安全
    qDebug() << "   ✓ 关闭未打开的源安全处理";
}

// 辅助方法实现
QString TestInputSource::createTestFile()
{
    qDebug() << "🔍 开始查找测试文件...";
    
    // 检查你指定的视频文件
    QString filename = "/Users/darkknight/Documents/learning resource/code/ffplayer0.1/Abracadabra.mp4";
    qDebug() << "   检查文件:" << filename;
    
    QFile file(filename);
    if (file.exists()) {
        qint64 fileSize = file.size();
        qDebug() << "   ✅ 文件存在，大小:" << fileSize << "字节";
        return filename;
    } else {
        qDebug() << "   ❌ 文件不存在:" << filename;
    }
    
    // 尝试其他可能的路径
    QStringList possible_files = {
        "/Users/darkknight/Desktop/test.mp4",
        "/Users/darkknight/Desktop/black.mp4",
        "/Users/darkknight/Movies/test.mp4",
        "/Users/darkknight/Movies/black.mp4",
        "/Users/darkknight/Downloads/test.mp4",
        "/Users/darkknight/Downloads/black.mp4"
    };
    
    for (const QString& path : possible_files) {
        qDebug() << "   检查备选文件:" << path;
        if (QFile::exists(path)) {
            qint64 fileSize = QFile(path).size();
            qDebug() << "   ✅ 找到文件，大小:" << fileSize << "字节";
            return path;
        }
    }
    
    qDebug() << "   ⚠️  未找到任何视频文件，创建临时测试文件";
    
    // 创建临时文件（仅用于基本测试）
    QString temp_dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString test_filename = QDir(temp_dir).filePath("ffplay_test_input.dat");
    
    QFile test_file(test_filename);
    if (test_file.open(QIODevice::WriteOnly)) {
        test_file.write("FFPLAY_TEST_INPUT_FILE_DATA");
        test_file.close();
        qDebug() << "   📝 创建临时文件:" << test_filename;
        return test_filename;
    }
    
    qDebug() << "   ❌ 所有方法都失败了";
    return QString();
}

void TestInputSource::removeTestFile(const QString& filename)
{
    QFile::remove(filename);
}

// Qt MOC 系统需要这个包含
#include "test_input_source.moc"
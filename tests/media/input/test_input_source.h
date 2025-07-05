#ifndef TEST_INPUT_SOURCE_H
#define TEST_INPUT_SOURCE_H

#include <QtTest>
#include <QObject>

// 包含完整的头文件，而不是前向声明
#include "media/input/input_source.h"
#include "media/input/file_input.h"
#include "media/input/rtsp_input.h"

class TestInputSource : public QObject
{
    Q_OBJECT

public:
    TestInputSource();

private slots:
    // Qt测试框架的标准方法
    void initTestCase();     // 在所有测试前执行一次
    void cleanupTestCase();  // 在所有测试后执行一次
    void init();             // 在每个测试前执行
    void cleanup();          // 在每个测试后执行
    
    // 具体的测试方法
    void testFactoryTypeDetection();
    void testFactoryCreation();
    void testFileInputBasic();
    void testFileInputError();
    void testRTSPInputBasic();
    void testErrorHandling();

private:
    // 测试辅助方法
    QString createTestFile();
    void removeTestFile(const QString& filename);
    
    // 测试数据
    QString test_file_path_;
};

#endif // TEST_INPUT_SOURCE_H
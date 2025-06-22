#ifndef SMART_POINTERS_H
#define SMART_POINTERS_H

#include <QObject>

// 自定义删除器：正确释放FFmpeg资源
// 工厂方法：便捷创建智能指针
// 转换函数：原始指针和智能指针互转
// 线程安全：支持多线程共享

class smart_pointers
{
    Q_OBJECT
public:
    smart_pointers();
};

#endif // SMART_POINTERS_H

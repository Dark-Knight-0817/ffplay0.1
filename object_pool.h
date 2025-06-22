#ifndef OBJECT_POOL_H
#define OBJECT_POOL_H

#include <QObject>

// 对象预创建：启动时创建一批对象
// 自动扩展：池空时自动创建新对象
// 对象重置：归还时自动调用reset方法
// 生命周期管理：RAII + 智能指针
// 性能监控：获取/归还次数、峰值使用量

class object_pool
{
    Q_OBJECT
public:
    object_pool();
};

#endif // OBJECT_POOL_H

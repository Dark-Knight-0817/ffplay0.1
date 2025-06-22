#ifndef PACKET_RECYCLER_H
#define PACKET_RECYCLER_H

#include <QObject>

// 大小分类：按packet大小分不同的池
// 引用计数：支持packet的共享使用
// 批量回收：减少锁竞争
// 内存压缩：定期整理碎片
// 统计分析：大小分布、使用模式

class packet_recycler
{
    Q_OBJECT
public:
    packet_recycler();
};

#endif // PACKET_RECYCLER_H

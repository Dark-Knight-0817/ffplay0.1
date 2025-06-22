#ifndef FRAME_ALLOCATOR_H
#define FRAME_ALLOCATOR_H

#include <QObject>

// 按分辨率分类：常见分辨率的专用池
// 格式支持：YUV420P、RGB24、NV12等格式
// 内存对齐：AVX512对齐优化
// 零拷贝优化：GPU内存映射支持
// 预分配策略：根据视频参数预分配

class frame_allocator
{
    Q_OBJECT
public:
    frame_allocator();
};

#endif // FRAME_ALLOCATOR_H

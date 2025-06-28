# FFplay0.1 学习路线图

> **文档位置**: `docs/learning_roadmap.md`  
> **创建时间**: 2025年6月  
> **目标**: 通过开发FFmpeg播放器深入学习音视频技术和高性能C++编程

## 📋 学习目标

通过实现一个完整的FFmpeg视频播放器，掌握以下核心技能：
- 🧠 **高性能内存管理**: 内存池、对象池、智能指针
- 🎥 **音视频处理技术**: FFmpeg API、解码、渲染
- ⚡ **性能优化**: SIMD、多线程、缓存优化
- 🎨 **现代UI开发**: Qt/QML、Material Design
- 🔧 **工程实践**: 测试驱动开发、性能分析、文档编写

## 🗺 学习路线

### 🚀 第一阶段：内存管理专家养成 (Week 1-4)

#### Week 1-2: 核心基础设施 ⭐⭐⭐

**学习重点**:
```cpp
// 主要文件
src/memory/frame_allocator.*    // 视频帧专用分配器
src/memory/memory_pool.*        // 通用内存池
tests/memory/unit/              // 基础单元测试
```

**核心概念学习**:
1. **内存对齐原理**
   - SIMD指令集要求（SSE: 16字节，AVX: 32字节）
   - CPU缓存行优化（通常64字节）
   - FFmpeg帧缓冲对齐需求

2. **视频帧内存布局**
   ```cpp
   // YUV420P格式内存布局学习
   struct VideoFrame {
       uint8_t* y_plane;    // 亮度平面
       uint8_t* u_plane;    // 色度平面U  
       uint8_t* v_plane;    // 色度平面V
       int y_stride;        // Y平面行字节数
       int uv_stride;       // UV平面行字节数
   };
   ```

3. **内存池设计模式**
   - 分层分配策略（小/中/大块）
   - 空闲链表管理
   - 线程安全机制

**实践任务**:
- [ ] 实现基础的FrameAllocator类
- [ ] 编写内存对齐测试用例
- [ ] 对比系统malloc的性能差异
- [ ] 学习使用perf/valgrind等工具

**学习资源**:
- 《Computer Systems: A Programmer's Perspective》第9章 - 虚拟内存
- Intel指令集手册 - SIMD对齐要求
- FFmpeg源码: `libavutil/mem.c` - av_malloc实现

#### Week 3-4: FFmpeg特化组件 ⭐⭐

**学习重点**:
```cpp
src/memory/packet_recycler.*   // AVPacket对象池
src/memory/cache_manager.*     // 解码帧缓存  
tests/memory/benchmark/        // 性能基准测试
```

**核心概念学习**:
1. **FFmpeg数据结构**
   ```cpp
   // 深入理解AVPacket生命周期
   AVPacket* pkt = av_packet_alloc();
   av_read_frame(fmt_ctx, pkt);     // 读取包
   avcodec_send_packet(dec_ctx, pkt); // 发送到解码器
   av_packet_unref(pkt);            // 释放引用
   av_packet_free(&pkt);            // 释放对象
   ```

2. **对象池设计模式**
   - 对象重用策略
   - 池大小动态调整
   - 内存增长控制

3. **缓存算法实现**
   - LRU (Least Recently Used)
   - LFU (Least Frequently Used)  
   - 缓存命中率优化

**实践任务**:
- [ ] 实现AVPacket对象池
- [ ] 设计LRU帧缓存
- [ ] 编写性能基准测试
- [ ] 分析内存使用模式

**验收标准**:
- 包回收率 > 95%
- 缓存命中率 > 80%
- 内存使用稳定（无泄漏）
- 并发访问安全

### 🎥 第二阶段：FFmpeg技术深入 (Week 5-8)

#### Week 5-6: 解码器核心

**学习重点**:
```cpp
src/media/decoder/video_decoder.*  // 视频解码器
src/media/decoder/audio_decoder.*  // 音频解码器
src/core/engine/decode_thread.*    // 解码线程管理
```

**核心概念学习**:
1. **FFmpeg解码流程**
   ```cpp
   // 完整的解码流程学习
   avformat_open_input() -> avformat_find_stream_info() -> 
   avcodec_find_decoder() -> avcodec_open2() ->
   av_read_frame() -> avcodec_send_packet() -> avcodec_receive_frame()
   ```

2. **多线程解码架构**
   - 生产者-消费者模式
   - 线程池管理
   - 任务队列设计

3. **硬件加速集成**
   - VAAPI (Linux)
   - DXVA2 (Windows)  
   - VideoToolbox (macOS)

**实践任务**:
- [ ] 实现基础视频解码器
- [ ] 集成内存管理组件
- [ ] 实现多线程解码
- [ ] 添加硬件加速支持

#### Week 7-8: 渲染和音频输出

**学习重点**:
```cpp
src/media/renderer/video_renderer.* // 视频渲染
src/media/audio/audio_output.*      // 音频输出
src/core/engine/player_engine.*    // 播放引擎
```

**核心概念学习**:
1. **视频渲染技术**
   - OpenGL纹理渲染
   - 颜色空间转换
   - 垂直同步控制

2. **音频输出处理**
   - 音频设备管理
   - 缓冲区管理
   - 重采样处理

3. **音视频同步**
   - PTS/DTS时间戳
   - 同步策略选择
   - 延迟补偿

### 🎨 第三阶段：用户界面与优化 (Week 9-12)

#### Week 9-10: QML现代界面

**学习重点**:
```cpp
resources/qml/PlayerWindow.qml      // 主播放界面
resources/qml/ControlPanel.qml      // 控制面板
src/ui/player/player_controller.*   // 播放控制器
```

**核心概念学习**:
1. **QML高级特性**
   - 自定义组件开发
   - 属性绑定与动画
   - 状态管理模式

2. **C++/QML集成**
   - Q_PROPERTY声明
   - 信号槽机制
   - 性能优化技巧

#### Week 11-12: 性能监控与文档

**学习重点**:
```cpp
src/ui/widgets/MemoryMonitor.qml    // 内存监控界面
src/utils/logger/performance_logger.* // 性能日志
docs/performance/                   // 性能分析报告
```

## 📊 学习评估标准

### 技术指标
| 组件 | 性能目标 | 测试方法 | 学习验证 |
|------|----------|----------|----------|
| FrameAllocator | 比malloc快50%+ | benchmark测试 | ✅理解内存对齐 |
| PacketRecycler | 回收率95%+ | 长期运行测试 | ✅掌握对象池 |
| MemoryPool | 碎片率<20% | 碎片分析 | ✅理解内存管理 |
| VideoDecoder | 1080p流畅播放 | 实际播放测试 | ✅掌握FFmpeg |
| AudioOutput | 延迟<100ms | 音频分析 | ✅理解音频处理 |

### 知识点掌握检查

#### 内存管理
- [ ] 能解释SIMD对齐的原理和必要性
- [ ] 能设计适合特定场景的内存池
- [ ] 理解内存碎片产生原因和解决方案
- [ ] 掌握多线程内存安全编程

#### FFmpeg技术
- [ ] 理解FFmpeg的基本架构和数据流
- [ ] 能实现自定义的解码器封装
- [ ] 掌握音视频同步的多种策略
- [ ] 了解硬件加速的集成方法

#### 性能优化
- [ ] 能使用性能分析工具定位瓶颈
- [ ] 理解CPU缓存对性能的影响
- [ ] 掌握多线程编程的最佳实践
- [ ] 能进行系统级性能调优

## 🧪 测试驱动学习

### 测试分层策略
```
tests/
├── unit/                    # 单元测试 - 验证基础功能
│   ├── memory/              # 内存管理组件测试
│   ├── decoder/             # 解码器组件测试
│   └── utils/               # 工具类测试
├── integration/             # 集成测试 - 验证组件协作
│   ├── memory_integration/  # 内存组件集成
│   └── player_integration/  # 播放器集成
├── performance/             # 性能测试 - 验证性能指标
│   ├── memory_benchmark/    # 内存性能基准
│   └── decode_benchmark/    # 解码性能基准
└── system/                  # 系统测试 - 验证完整功能
    ├── playback_test/       # 播放功能测试
    └── stress_test/         # 压力测试
```

### 学习验证方法
1. **代码审查**: 每周进行代码review，检查设计合理性
2. **性能对比**: 每个组件都要与基线实现对比
3. **文档输出**: 记录学习心得和技术总结
4. **问题解决**: 遇到问题的分析和解决过程

## 📚 学习资源库

### 必读书籍
1. **《Effective C++》** - Scott Meyers
   - 重点章节：条款13-17（资源管理）
   - 学习目标：掌握RAII和智能指针

2. **《C++ Concurrency in Action》** - Anthony Williams
   - 重点章节：第6-9章（无锁数据结构）
   - 学习目标：理解多线程内存模型

3. **《Computer Systems: A Programmer's Perspective》**
   - 重点章节：第6章（存储器层次结构）
   - 学习目标：理解缓存和内存性能

### 在线资源
1. **FFmpeg官方文档**
   - API Reference: https://ffmpeg.org/doxygen/trunk/
   - Examples: https://github.com/FFmpeg/FFmpeg/tree/master/doc/examples

2. **Intel优化手册**
   - Intel 64 and IA-32 Architectures Optimization Reference Manual
   - 学习SIMD优化技巧

3. **Qt官方文档**
   - QML教程: https://doc.qt.io/qt-6/qmlapplications.html
   - 性能指南: https://doc.qt.io/qt-6/qtquick-performance.html

### 参考项目
1. **VLC播放器** - https://github.com/videolan/vlc
   - 学习重点：模块化架构设计
   
2. **mpv播放器** - https://github.com/mpv-player/mpv  
   - 学习重点：高性能实现技巧

3. **FFmpeg项目本身**
   - 学习重点：内存管理实现（libavutil/mem.c）

## 📝 学习记录模板

### 每周学习总结
```markdown
## Week X 学习总结

### 完成的任务
- [ ] 任务1描述
- [ ] 任务2描述

### 技术收获
1. **核心概念理解**：
   - 概念名称：具体理解内容

2. **编程技能提升**：
   - 技能点：具体改进内容

### 遇到的问题
1. **问题描述**：
   - 问题具体情况
   - 解决方案
   - 学到的经验

### 性能数据
- 性能指标：具体数值
- 对比基准：数据对比

### 下周计划
- [ ] 下周任务1
- [ ] 下周任务2
```

### 技术难点记录
```markdown
## 技术难点：[问题标题]

### 问题描述
具体描述遇到的技术问题

### 分析过程  
1. 问题分析步骤
2. 尝试的解决方案
3. 失败的原因

### 解决方案
最终采用的解决方案和实现细节

### 经验总结
这个问题的核心原理和预防方法
```

## 🎯 阶段性里程碑

### Milestone 1: 内存管理专家 (Week 4结束)
**验收标准**:
- [ ] 完成frame_allocator核心功能
- [ ] 实现packet_recycler对象池
- [ ] 所有内存组件通过单元测试
- [ ] 性能超越系统分配30%+
- [ ] 编写技术总结文档

**学习成果**:
- 深度理解内存管理原理
- 掌握高性能C++编程技巧
- 具备系统性能分析能力

### Milestone 2: FFmpeg解码专家 (Week 8结束)
**验收标准**:
- [ ] 实现完整的视频解码流程
- [ ] 支持主流视频格式
- [ ] 实现多线程解码
- [ ] 音视频同步误差<40ms
- [ ] 编写FFmpeg技术文档

**学习成果**:
- 熟练掌握FFmpeg API
- 理解音视频编解码原理
- 具备多媒体应用开发能力

### Milestone 3: 完整产品交付 (Week 12结束)  
**验收标准**:
- [ ] 完整的播放器界面
- [ ] 实时性能监控功能
- [ ] 完整的测试覆盖
- [ ] 性能分析报告
- [ ] 完整的项目文档

**学习成果**:
- 具备完整的项目开发能力
- 掌握现代C++和Qt开发
- 形成系统的技术文档体系

## 📈 持续改进

### 学习反馈循环
1. **每日反思**: 记录当天的学习收获和问题
2. **每周总结**: 整理技术要点和性能数据
3. **阶段review**: 评估学习目标达成情况
4. **调整计划**: 根据学习进度调整后续安排

### 技术分享
1. **内部分享**: 团队内部的技术分享
2. **博客文章**: 技术博客记录学习心得
3. **开源贡献**: 向相关开源项目贡献代码
4. **技术社区**: 参与技术讨论和问答

---

**开始你的FFmpeg技术专家之路！** 🚀

> 记住：学习不仅仅是完成功能，更重要的是理解背后的原理和设计思想。每一行代码都是对技术理解的体现。
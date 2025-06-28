# ffplay0.1

基于FFmpeg的简单视频播放器，使用Qt + QML开发

## ✨ 特性
- 🎥 支持多种视频格式
- 🎵 音频播放支持
- 🧠 智能内存管理
- 🎨 现代化QML界面

## 📁 项目结构

```
ffplay0.1/
├── 📁 src/             # 源代码
│   ├── memory/         # 内存管理模块
│   ├── media/          # 媒体处理
│   └── utils/          # 工具类
├── 📁 resources/       # 资源文件
│   ├── qml/           # QML界面
│   ├── images/        # 图片资源
│   └── styles/        # 样式文件
├── 📁 tests/          # 测试代码
├── 📁 tools/          # 构建工具
└── 📁 docs/           # 文档
```

## 🚀 快速开始

### 环境要求
- Qt 5.15+ 或 Qt 6.x
- FFmpeg 4.0+
- CMake 3.16+

### 构建步骤
```bash
# 克隆项目
git clone https://github.com/your-username/ffplay0.1.git
cd ffplay0.1

# 构建
mkdir build && cd build
cmake ..
make

# 运行
./Project_Disassembly
```

### Windows构建
```cmd
mkdir build
cd build
cmake .. -G "Visual Studio 16 2019"
cmake --build . --config Release
```

## 🔧 配置FFmpeg

### macOS (Homebrew)
```bash
brew install ffmpeg
```

### Windows
1. 下载FFmpeg预编译版本
2. 修改CMakeLists.txt中的`FFMPEG_ROOT`路径

### Linux
```bash
sudo apt install libavcodec-dev libavformat-dev libavutil-dev
```

## 📖 核心模块

| 模块 | 描述 | 状态 |
|------|------|------|
| 🧠 Memory | 内存池、对象池、智能指针 | ✅ 已完成 |
| 🎥 Media | 视频解码、音频处理 | 🚧 开发中 |
| 🎨 UI | QML界面、播放控制 | 🚧 开发中 |
| 🛠 Utils | 日志、文件工具 | 📋 计划中 |

## 📝 开发说明

### 添加新功能
1. 在对应的`src/`子目录下创建文件
2. 更新`CMakeLists.txt`中的源文件列表
3. 编写测试用例

### 代码规范
- 使用C++17标准
- 遵循Qt编码规范
- 头文件使用`.h`，实现文件使用`.cpp`

## 🤝 贡献

欢迎提交Issue和Pull Request！

## 📄 许可证

MIT License
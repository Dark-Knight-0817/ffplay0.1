# ffplay0.1
base on MyPlay which is very simple video player

## 目录结构
Project_Disassembly/
├── CMakeLists.txt                    # 主CMake配置文件（已存在）
├── main.cpp                          # 主程序入口（已存在）
├── qml.qrc                          # Qt资源文件（需要更新路径）
├── README.md                        # 项目说明文档（已存在）
├── .gitignore                       # Git忽略文件
├──
├── src/                             # 主要源代码目录
│   ├── core/                        # 核心功能模块
│   │   ├── main_window.h            # 主窗口头文件（如果有的话）
│   │   ├── main_window.cpp          # 主窗口实现（如果有的话）
│   │   └── app_config.h             # 应用配置（如果有的话）
│   │
│   ├── memory/                      # 内存管理模块（已完成）
│   │   ├── memory_pool.h
│   │   ├── memory_pool.cpp
│   │   ├── memory_manager.h
│   │   ├── memory_manager.cpp
│   │   ├── memory_tracker.h
│   │   ├── memory_tracker.cpp
│   │   ├── smart_pointers.h
│   │   ├── smart_pointers.cpp
│   │   ├── cache_manager.h
│   │   ├── cache_manager.cpp
│   │   ├── frame_allocator.h
│   │   ├── frame_allocator.cpp
│   │   ├── object_pool.h
│   │   ├── object_pool.cpp
│   │   ├── packet_recycler.h
│   │   └── packet_recycler.cpp
│   │
│   ├── media/                       # 媒体处理模块
│   │   ├── video_decoder.h
│   │   ├── video_decoder.cpp
│   │   ├── audio_decoder.h
│   │   └── audio_decoder.cpp
│   │
│   └── utils/                       # 工具类
│       ├── logger.h
│       ├── logger.cpp
│       ├── file_utils.h
│       └── file_utils.cpp
│
├── include/                         # 公共头文件目录
│   └── common/                      # 公共定义
│       ├── types.h                  # 公共类型定义
│       ├── constants.h              # 常量定义
│       └── macros.h                 # 公共宏定义
│
├── resources/                       # 资源文件目录
│   ├── qml/                        # QML文件
│   │   ├── main.qml                # 主QML文件（已移动）
│   │   ├── components/             # QML组件
│   │   │   ├── VideoPlayer.qml
│   │   │   ├── ControlPanel.qml
│   │   │   └── MediaInfo.qml
│   │   └── pages/                  # QML页面
│   │       ├── HomePage.qml
│   │       └── SettingsPage.qml
│   │
│   ├── images/                     # 图片资源
│   │   ├── icons/
│   │   └── backgrounds/
│   │
│   ├── styles/                     # 样式文件
│   │   ├── AppTheme.qml
│   │   └── Colors.qml
│   │
│   └── data/                       # 数据文件
│       └── config.json
│
├── tests/                          # 测试文件目录
│   ├── CMakeLists.txt              # 测试专用CMake
│   ├── test_main.cpp               # 测试主入口
│   ├── memory/                     # 内存模块测试
│   │   ├── test_memory_pool.cpp
│   │   └── test_memory_manager.cpp
│   └── media/                      # 媒体模块测试
│       └── test_video_decoder.cpp
│
├── tools/                          # 工具脚本目录
│   ├── scripts/                    # 构建脚本
│   │   ├── build.sh               # Unix构建脚本
│   │   ├── build.bat              # Windows构建脚本
│   │   └── clean.sh               # 清理脚本
│   │
│   └── cmake/                      # CMake模块
│       ├── FindFFmpeg.cmake       # FFmpeg查找模块
│       ├── CompilerFlags.cmake     # 编译器标志
│       └── InstallConfig.cmake     # 安装配置
│
├── docs/                           # 文档目录
│   ├── api/                        # API文档
│   ├── design/                     # 设计文档
│   └── user_manual.md              # 用户手册
│
├── build/                          # 构建输出目录（.gitignore）
├── install/                        # 安装目录（.gitignore）
└── temp/                           # 临时文件目录（.gitignore）
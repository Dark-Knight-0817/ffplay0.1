cmake_minimum_required(VERSION 3.16)
project(Project_Disassembly VERSION 0.1 LANGUAGES CXX)

# Qt相关设置
set(CMAKE_AUTOUIC ON)    # 自动处理.ui文件
set(CMAKE_AUTOMOC ON)    # 自动处理Q_OBJECT宏
set(CMAKE_AUTORCC ON)    # 自动处理.qrc资源文件
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找Qt库
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Quick)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Quick)

# FFmpeg 配置（保持原有逻辑不变）
if(WIN32)
    set(FFMPEG_ROOT "E:/FFmpeg/ffmpeg-7.1.1-full_build-shared")
    set(FFMPEG_INCLUDE_DIRS "${FFMPEG_ROOT}/include")
    set(FFMPEG_LIBRARY_DIRS "${FFMPEG_ROOT}/lib")

    # 检查包含目录是否存在
    if(NOT EXISTS ${FFMPEG_INCLUDE_DIRS})
        message(FATAL_ERROR "FFmpeg包含目录不存在: ${FFMPEG_INCLUDE_DIRS}")
    endif()

    # 检查关键头文件是否存在
    if(NOT EXISTS "${FFMPEG_INCLUDE_DIRS}/libavformat/avformat.h")
        message(FATAL_ERROR "找不到 libavformat/avformat.h 在: ${FFMPEG_INCLUDE_DIRS}")
    endif()

    # 查找库文件
    find_library(AVCODEC_LIBRARY avcodec PATHS ${FFMPEG_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_library(AVFORMAT_LIBRARY avformat PATHS ${FFMPEG_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_library(AVUTIL_LIBRARY avutil PATHS ${FFMPEG_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_library(SWSCALE_LIBRARY swscale PATHS ${FFMPEG_LIBRARY_DIRS} NO_DEFAULT_PATH)
    find_library(SWRESAMPLE_LIBRARY swresample PATHS ${FFMPEG_LIBRARY_DIRS} NO_DEFAULT_PATH)

    # 检查是否找到所有必需的库
    if(NOT AVCODEC_LIBRARY OR NOT AVFORMAT_LIBRARY OR NOT AVUTIL_LIBRARY OR NOT SWSCALE_LIBRARY)
        message(FATAL_ERROR "找不到FFmpeg库文件，请检查路径: ${FFMPEG_LIBRARY_DIRS}")
    endif()

    set(FFMPEG_LIBRARIES
        ${AVCODEC_LIBRARY}
        ${AVFORMAT_LIBRARY}
        ${AVUTIL_LIBRARY}
        ${SWSCALE_LIBRARY}
        ${SWRESAMPLE_LIBRARY}
    )

    # 输出调试信息
    message(STATUS "FFmpeg根目录: ${FFMPEG_ROOT}")
    message(STATUS "FFmpeg包含目录: ${FFMPEG_INCLUDE_DIRS}")
    message(STATUS "FFmpeg库目录: ${FFMPEG_LIBRARY_DIRS}")
    message(STATUS "找到的库: ${FFMPEG_LIBRARIES}")

elseif(APPLE)
    # macOS 使用 Homebrew 安装的 FFmpeg
    find_package(PkgConfig REQUIRED)

    # 设置 pkg-config 路径（Homebrew 安装位置）
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
        # Apple Silicon Mac
        set(ENV{PKG_CONFIG_PATH} "/opt/homebrew/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
        set(HOMEBREW_PREFIX "/opt/homebrew")
    else()
        # Intel Mac
        set(ENV{PKG_CONFIG_PATH} "/usr/local/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
        set(HOMEBREW_PREFIX "/usr/local")
    endif()

    pkg_check_modules(FFMPEG REQUIRED
        libavcodec
        libavformat
        libavutil
        libswscale
        libswresample
    )

    message(STATUS "macOS FFmpeg 配置:")
    message(STATUS "  包含目录: ${FFMPEG_INCLUDE_DIRS}")
    message(STATUS "  库目录: ${FFMPEG_LIBRARY_DIRS}")
    message(STATUS "  库文件: ${FFMPEG_LIBRARIES}")
else()
    # Linux 使用 pkg-config
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED
        libavcodec
        libavformat
        libavutil
        libswscale
        libswresample
    )
endif()

# ============ 源文件组织（按新目录结构） ============

# 主程序文件（项目根目录）
set(MAIN_SOURCES
    main.cpp
    qml.qrc        # 注意：qml.qrc里的路径需要更新为resources/qml/xxx
)

# src/memory/ 目录下的内存管理模块
# 使用 file(GLOB) 或手动列出，推荐手动列出（更明确）
set(MEMORY_SOURCES
    src/memory/memory_pool.cpp
    # 如果你有其他memory模块的.cpp文件，在这里添加
    # src/memory/memory_manager.cpp
    # src/memory/cache_manager.cpp
    # 等等...
)

# 注意：.h头文件不需要在这里列出，CMake会自动处理
# 但是你需要确保include路径正确

# 如果src/core, src/media, src/utils有.cpp文件，也要加上
# set(CORE_SOURCES
#     src/core/xxx.cpp
# )

# set(MEDIA_SOURCES
#     src/media/xxx.cpp
# )

# set(UTILS_SOURCES
#     src/utils/xxx.cpp
# )

# 合并所有源文件
set(ALL_SOURCES
    ${MAIN_SOURCES}
    ${MEMORY_SOURCES}
    # ${CORE_SOURCES}      # 当你有这些文件时取消注释
    # ${MEDIA_SOURCES}
    # ${UTILS_SOURCES}
)

# 输出调试信息，看看找到了哪些源文件
message(STATUS "找到的源文件: ${ALL_SOURCES}")

# ============ 创建可执行文件 ============
if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(Project_Disassembly
        MANUAL_FINALIZATION
        ${ALL_SOURCES}
    )
else()
    if(ANDROID)
        add_library(Project_Disassembly SHARED
            ${ALL_SOURCES}
        )
    else()
        add_executable(Project_Disassembly
            ${ALL_SOURCES}
        )
    endif()
endif()

# ============ 设置包含目录 ============
# 让编译器能找到头文件
target_include_directories(Project_Disassembly PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}          # 项目根目录（为了找到main.cpp等）
    ${CMAKE_CURRENT_SOURCE_DIR}/src      # src目录（为了用 #include "memory/xxx.h"）
    ${CMAKE_CURRENT_SOURCE_DIR}/include  # include目录（如果你有公共头文件）
    ${FFMPEG_INCLUDE_DIRS}               # FFmpeg头文件目录
)

# 这样设置后，在代码中可以：
# #include "memory/memory_pool.h"  // 因为设置了src目录
# 或者 #include "common/types.h"   // 如果在include目录下

# ============ 编译选项 ============
target_compile_definitions(Project_Disassembly PRIVATE
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>
)

# ============ 链接库 ============
# 链接Qt库
target_link_libraries(Project_Disassembly PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Quick
)

# 链接FFmpeg库
if(WIN32)
    target_link_libraries(Project_Disassembly PRIVATE ${FFMPEG_LIBRARIES})
else()
    # macOS 和 Linux
    target_link_libraries(Project_Disassembly PRIVATE ${FFMPEG_LIBRARIES})
    target_include_directories(Project_Disassembly PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_directories(Project_Disassembly PRIVATE ${FFMPEG_LIBRARY_DIRS})
endif()

# ============ 平台特定设置 ============

# Windows 特定设置
if(WIN32)
    # 添加 FFmpeg DLL 路径到运行时路径
    set_target_properties(Project_Disassembly PROPERTIES
        VS_DEBUGGER_ENVIRONMENT "PATH=${FFMPEG_ROOT}/bin;$ENV{PATH}"
    )

    # 复制 FFmpeg DLL 到输出目录（这样运行时能找到DLL）
    add_custom_command(TARGET Project_Disassembly POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FFMPEG_ROOT}/bin/avcodec-61.dll"
        "${FFMPEG_ROOT}/bin/avformat-61.dll"
        "${FFMPEG_ROOT}/bin/avutil-59.dll"
        "${FFMPEG_ROOT}/bin/swscale-8.dll"
        "${FFMPEG_ROOT}/bin/swresample-5.dll"
        $<TARGET_FILE_DIR:Project_Disassembly>
        COMMENT "复制 FFmpeg DLL 文件"
    )
endif()

# macOS 特定设置
if(APPLE)
    set_target_properties(Project_Disassembly PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    )
endif()

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
if(${QT_VERSION} VERSION_LESS 6.1.0)
    set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.Project_Disassembly)
endif()

set_target_properties(Project_Disassembly PROPERTIES
    ${BUNDLE_ID_OPTION}
    WIN32_EXECUTABLE TRUE
)

# ============ 安装配置 ============
include(GNUInstallDirs)
install(TARGETS Project_Disassembly
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# Qt6特定的最终化步骤
if(QT_VERSION_MAJOR EQUAL 6)
    qt_import_qml_plugins(Project_Disassembly)
    qt_finalize_executable(Project_Disassembly)
endif()

# ============ 可选：测试构建 ============
# 目前tests目录为空，所以注释掉
option(BUILD_TESTS "Build tests" OFF)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

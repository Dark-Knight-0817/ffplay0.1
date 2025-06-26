cmake_minimum_required(VERSION 3.16)
project(Project_Disassembly VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Quick)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Quick)

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

else()
    # Linux/macOS 使用 pkg-config
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED
        libavcodec
        libavformat
        libavutil
        libswscale
        libswresample
    )
endif()

set(PROJECT_SOURCES
    main.cpp
    qml.qrc
)

# 内存管理模块源文件
set(MEMORY_MANAGEMENT_SOURCES
    memory_pool.h memory_pool.cpp
    memory_tracker.h memory_tracker.cpp
    smart_pointers.h smart_pointers.cpp
    # object_pool.h object_pool.cpp              # 待实现
    # frame_allocator.h frame_allocator.cpp      # 待实现
    # packet_recycler.h packet_recycler.cpp      # 待实现
    # cache_manager.h cache_manager.cpp          # 待实现
    # memory_manager.h memory_manager.cpp        # 待实现
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(Project_Disassembly
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        ${MEMORY_MANAGEMENT_SOURCES}
    )
else()
    if(ANDROID)
        add_library(Project_Disassembly SHARED
            ${PROJECT_SOURCES}
            ${MEMORY_MANAGEMENT_SOURCES}
        )
    else()
        add_executable(Project_Disassembly
            ${PROJECT_SOURCES}
            ${MEMORY_MANAGEMENT_SOURCES}
        )
    endif()
endif()

# 设置包含目录 - 这是关键！
target_include_directories(Project_Disassembly PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${FFMPEG_INCLUDE_DIRS}
)

# 设置编译定义
target_compile_definitions(Project_Disassembly PRIVATE
    $<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:QT_QML_DEBUG>
)

# 链接库
target_link_libraries(Project_Disassembly PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Quick
    ${FFMPEG_LIBRARIES}
)

# Windows 特定设置
if(WIN32)
    # 添加 FFmpeg DLL 路径到运行时路径
    set_target_properties(Project_Disassembly PROPERTIES
        VS_DEBUGGER_ENVIRONMENT "PATH=${FFMPEG_ROOT}/bin;$ENV{PATH}"
    )

    # 复制 FFmpeg DLL 到输出目录（可选）
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

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
if(${QT_VERSION} VERSION_LESS 6.1.0)
    set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.Project_Disassembly)
endif()

set_target_properties(Project_Disassembly PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

include(GNUInstallDirs)
install(TARGETS Project_Disassembly
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_import_qml_plugins(Project_Disassembly)
    qt_finalize_executable(Project_Disassembly)
endif()

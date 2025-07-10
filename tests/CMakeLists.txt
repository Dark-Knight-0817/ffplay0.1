# tests/CMakeLists.txt - 添加输入源测试支持

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

# 测试源文件（包含cpp实现文件）
set(TEST_SOURCES
    main.cpp
    memory/test_memory_pool.cpp
    memory/test_pool_performance.cpp
)

# 被测试的源文件
set(TESTED_SOURCES
    # 内存池模块
    ../src/memory/memory_pool.cpp
)

# 检查FFmpeg可用性，决定是否编译FFmpeg相关测试
if(FFMPEG_FOUND OR FFMPEG_LIBRARIES)
    message(STATUS "✅ FFmpeg可用，启用FFmpeg相关测试")
    
    # 添加FFmpeg相关测试源文件
    list(APPEND TEST_SOURCES
        media/allocator/test_ffmpeg_frame_allocator.cpp
        media/input/test_input_source.cpp  # 新增输入源测试
    )
    
    # 添加FFmpeg相关被测试源文件
    list(APPEND TESTED_SOURCES
        # Frame Allocator模块
        ../src/media/allocator/frame_allocator_factory.cpp
        ../src/media/allocator/ffmpeg_allocator/ffmpeg_frame_allocator.cpp
        
        # 输入源模块
        ../src/media/input/input_source.cpp
        ../src/media/input/file_input.cpp
        ../src/media/input/rtsp_input.cpp
        # 工具模块 - 添加这行！
        ../src/utils/network_detector.cpp   
    )
    
    message(STATUS "  包含模块: Frame Allocator + 输入源")
else()
    message(STATUS "⚠️  FFmpeg不可用，跳过FFmpeg相关测试")
endif()

# 输出将要编译的测试文件
message(STATUS "测试源文件: ${TEST_SOURCES}")
message(STATUS "被测试源文件: ${TESTED_SOURCES}")

# 创建测试可执行文件
add_executable(run_tests 
    ${TEST_SOURCES}
    ${TESTED_SOURCES}
)

# 设置包含目录
target_include_directories(run_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..        # 项目根目录
    ${CMAKE_CURRENT_SOURCE_DIR}/../src    # src目录，用于 #include "media/xxx/xxx.h"
)

# 如果有FFmpeg，添加FFmpeg相关的包含和链接
if(FFMPEG_FOUND OR FFMPEG_LIBRARIES)
    target_include_directories(run_tests PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_compile_definitions(run_tests PRIVATE FFMPEG_AVAILABLE)
    
    # 链接FFmpeg库
    if(WIN32)
        target_link_libraries(run_tests PRIVATE ${FFMPEG_LIBRARIES})
    else()
        target_link_libraries(run_tests PRIVATE ${FFMPEG_LIBRARIES})
        target_link_directories(run_tests PRIVATE ${FFMPEG_LIBRARY_DIRS})
    endif()
endif()

# 链接Qt库
target_link_libraries(run_tests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Test
)

# 注册到CTest
add_test(NAME memory_pool_tests COMMAND run_tests memory)

if(FFMPEG_FOUND OR FFMPEG_LIBRARIES)
    add_test(NAME frame_allocator_tests COMMAND run_tests allocator)
    add_test(NAME input_source_tests COMMAND run_tests input)
    add_test(NAME media_tests COMMAND run_tests media)
endif()

add_test(NAME all_tests COMMAND run_tests)

# 设置测试超时
set_tests_properties(all_tests PROPERTIES TIMEOUT 180)

if(FFMPEG_FOUND OR FFMPEG_LIBRARIES)
    set_tests_properties(memory_pool_tests frame_allocator_tests input_source_tests media_tests PROPERTIES TIMEOUT 120)
else()
    set_tests_properties(memory_pool_tests PROPERTIES TIMEOUT 120)
endif()

# 输出构建信息
message(STATUS "测试配置: 头文件分离的标准C++结构")
if(FFMPEG_FOUND OR FFMPEG_LIBRARIES)
    message(STATUS "  FFmpeg Frame Allocator: ✅ 启用")
    message(STATUS "  输入源模块: ✅ 启用")
    message(STATUS "  支持的输入类型: 本地文件 + RTSP流")
else()
    message(STATUS "  FFmpeg相关模块: ❌ 禁用")
endif()

# 添加编译选项
target_compile_features(run_tests PRIVATE cxx_std_17)

# 如果是Debug模式，添加调试信息
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(run_tests PRIVATE DEBUG_BUILD)
    if(NOT WIN32)
        target_compile_options(run_tests PRIVATE -g -O0)
    endif()
endif()
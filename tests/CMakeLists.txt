# tests/CMakeLists.txt - 添加frame allocator测试支持

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

# 测试源文件（包含cpp实现文件）
set(TEST_SOURCES
    main.cpp
    memory/test_memory_pool.cpp
    memory/test_pool_performance.cpp
    media/allocator/test_ffmpeg_frame_allocator.cpp  # 新增frame allocator测试
)

# 被测试的源文件
set(TESTED_SOURCES
    # 内存池模块
    ../src/memory/memory_pool.cpp
    
    # Frame Allocator模块
    ../src/media/allocator/frame_allocator_factory.cpp
    ../src/media/allocator/ffmpeg_allocator/ffmpeg_frame_allocator.cpp
)

# 检查FFmpeg可用性，决定是否编译FFmpeg相关测试
# 在macOS上，FFMPEG_FOUND是正确的变量名
if(FFMPEG_FOUND OR FFMPEG_LIBRARIES)
    message(STATUS "✅ FFmpeg可用，启用FFmpeg Frame Allocator测试")
    message(STATUS "  FFmpeg库: ${FFMPEG_LIBRARIES}")
    # FFmpeg相关的编译定义已在主CMakeLists.txt中设置
else()
    message(STATUS "⚠️  FFmpeg不可用，跳过FFmpeg Frame Allocator测试")
    # 从测试源文件中移除FFmpeg相关测试
    list(REMOVE_ITEM TEST_SOURCES media/allocator/test_ffmpeg_frame_allocator.cpp)
    list(REMOVE_ITEM TESTED_SOURCES ../src/media/allocator/ffmpeg_allocator/ffmpeg_frame_allocator.cpp)
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
    ${CMAKE_CURRENT_SOURCE_DIR}/../src    # src目录，用于 #include "media/allocator/xxx.h"
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
add_test(NAME frame_allocator_tests COMMAND run_tests media)
add_test(NAME all_tests COMMAND run_tests)

# 设置测试超时
set_tests_properties(memory_pool_tests frame_allocator_tests all_tests PROPERTIES TIMEOUT 120)

# 输出构建信息
message(STATUS "测试配置: 头文件分离的标准C++结构 + Frame Allocator支持")
if(FFMPEG_FOUND OR FFMPEG_LIBRARIES)
    message(STATUS "  FFmpeg Frame Allocator: ✅ 启用")
else()
    message(STATUS "  FFmpeg Frame Allocator: ❌ 禁用")
endif()
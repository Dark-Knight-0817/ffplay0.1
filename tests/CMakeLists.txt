# tests/CMakeLists.txt - 头文件分离版本

find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

# 测试源文件（包含cpp实现文件）
set(TEST_SOURCES
    main.cpp
    memory/test_memory_pool.cpp
    memory/test_pool_performance.cpp
)

# 被测试的源文件
set(TESTED_SOURCES
    ../src/memory/memory_pool.cpp
)

# 创建测试可执行文件
add_executable(run_tests 
    ${TEST_SOURCES}
    ${TESTED_SOURCES}
)

# 设置包含目录
target_include_directories(run_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..        # 项目根目录
    ${CMAKE_CURRENT_SOURCE_DIR}/../src    # src目录
)

# 链接库
target_link_libraries(run_tests PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Test
)

# 注册到CTest
add_test(NAME memory_pool_all_tests COMMAND run_tests)

# 设置测试超时
set_tests_properties(memory_pool_all_tests PROPERTIES TIMEOUT 120)

# 输出构建信息
message(STATUS "测试配置: 头文件分离的标准C++结构")
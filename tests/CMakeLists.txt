# tests/CMakeLists.txt - 测试专用构建配置

# 查找Qt Test模块
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Test)

# 设置测试源文件
set(TEST_SOURCES
    test_main.cpp
    memory/test_memory_pool.cpp
)

# 需要测试的源文件（从主项目引用）
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

# 注册测试到CTest
add_test(NAME memory_pool_tests COMMAND run_tests)
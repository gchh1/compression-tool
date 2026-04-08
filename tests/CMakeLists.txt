# 将测试代码编译为一个可执行文件
# add_executable(test_Huffman test_Huffman.cpp)
# add_executable(test_Deflate test_Deflate.cpp)
add_executable(test test.cpp)

# 链接核心静态库
# target_link_libraries(test_Huffman PRIVATE algorithm_lib)
# target_link_libraries(test_Deflate PRIVATE compress_core)
target_link_libraries(test PRIVATE compress_core)

# 添加包含目录
# target_include_directories(test_Deflate PRIVATE ../src/core/include ../src/algorithm/include)
target_include_directories(test PRIVATE ../src/core/include ../src/algorithm/include)


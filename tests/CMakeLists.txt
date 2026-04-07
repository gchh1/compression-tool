# 将测试代码编译为一个可执行文件
add_executable(test_LZSS test_LZSS.cpp)
# add_executable(test_Huffman test_Huffman.cpp)

# 链接核心静态库
target_link_libraries(test_LZSS PRIVATE compress_core)
# target_link_libraries(test_Huffman PRIVATE algorithm_lib)


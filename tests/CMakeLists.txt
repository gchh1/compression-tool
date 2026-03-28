# 将测试代码编译为一个可执行文件
add_executable(test_LZ77 test_LZ77.cpp)

# 链接核心静态库
target_link_libraries(test_LZ77 PRIVATE algorithm_lib)

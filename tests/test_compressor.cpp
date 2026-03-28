#include <iostream>
#include <string>
#include <vector>

#include "dummy_compressor.hpp"  // 直接引入我们的核心头文件

using namespace compressor::core;

int main() {
    std::cout << "--- Web Compressor Test ---" << std::endl;

    // 1. 准备测试数据 (一段字符串)
    std::string test_str = "Hello, WebCompressor! This is a test string.";
    std::vector<uint8_t> input_data(test_str.begin(), test_str.end());

    // 2. 实例化压缩器 (利用多态，用父类指针指向子类对象)
    ICompressor* comp = new DummyCompressor();

    std::cout << "Algorithm: " << comp->get_algorithm_name() << std::endl;
    std::cout << "Original String: " << test_str << std::endl;

    // 3. 执行压缩
    CompressResult result = comp->compress(input_data);

    // 4. 打印任务书要求评估的各项指标
    std::cout << "Original Size: " << result.original_size << " bytes"
              << std::endl;
    std::cout << "Compressed Size: " << result.compressed_size << " bytes"
              << std::endl;
    std::cout << "Compression Ratio: " << result.compression_ratio << "%"
              << std::endl;
    std::cout << "Time elapsed: " << result.time_ms << " ms" << std::endl;

    // 清理内存
    delete comp;

    return 0;
}
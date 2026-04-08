#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

#include "DeflateCompressor.hpp"
#include "LZSSCompressor.hpp"
#include "compressor.hpp"

using namespace compressor::core;
using namespace compressor::algorithm;

// 辅助函数：将文件读取为字节流
std::vector<uint8_t> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return {};
    }
    // 移动到文件末尾获取大小，然后倒回头部
    file.seekg(0, std::ios::end);
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        return buffer;
    }
    return {};
}

// 辅助函数：将字节流保存为物理二进制文件
bool writeToFile(const std::string& filename,
                 const std::vector<uint8_t>& data) {
    // 必须使用 std::ios::binary 模式，防止操作系统弄乱换行符
    std::ofstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file for writing: " << filename
                  << std::endl;
        return false;
    }
    // 将 vector 中的数据一次性写入文件
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
    return true;
}

int main() {
    // LZSSCompressor engine;
    DeflateCompressor engine;

    std::string name = engine.get_algorithm_name();

    std::cout << "--- " << name << " File Compression Test ---" << std::endl;

    // 1. 读取网页文件（请确保路径正确，如果使用 CMake
    // 运行，可能需要传入绝对路径或放到 build 目录下）
    std::string filename = "../../tests/data/cmu445.html";
    std::vector<uint8_t> input_data = readFile(filename);

    if (input_data.empty()) {
        std::cerr << "Please check if the file exists!" << std::endl;
        return 1;
    }

    std::cout << "Original Size: " << input_data.size() << " bytes"
              << std::endl;

    // 2. 调用纯净版算法进行压缩
    CompressResult compressed_data = engine.compress(input_data);

    std::cout << "Compressed Size: " << compressed_data.compressed_size
              << " bytes" << std::endl;

    // 计算并打印压缩率
    std::cout << "Compression Ratio: " << compressed_data.compression_ratio
              << "%" << std::endl;

    // print time
    std::cout << "Compress Time: " << compressed_data.time_ms << "ms"
              << std::endl;

    // 3. 将压缩后的结果保存到本地磁盘
    std::string output_filename =
        "../../tests/data/cmu445.Deflate";  // 自定义一个酷炫的后缀名
    if (writeToFile(output_filename, compressed_data.data)) {
        std::cout << "\n🎉 Success! Compressed file generated at: "
                  << output_filename << std::endl;
    }

    // 4. 解压缩
    filename = "../../tests/data/cmu445_decompressed.html";
    CompressResult decompressed_data = engine.decompress(compressed_data.data);
    if (writeToFile(filename, decompressed_data.data)) {
        std::cout << "\n🎉 Success! Decompressed file generated at: "
                  << filename << std::endl;
    }

    return 0;
}

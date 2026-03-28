#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

#include "LZ77.hpp"

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
    std::cout << "--- LZ77 File Compression Test ---" << std::endl;

    // 1. 读取网页文件（请确保路径正确，如果使用 CMake
    // 运行，可能需要传入绝对路径或放到 build 目录下）
    std::string filename = "../../tests/data/test.html";
    std::vector<uint8_t> input_data = readFile(filename);

    if (input_data.empty()) {
        std::cerr << "Please check if the file exists!" << std::endl;
        return 1;
    }

    std::cout << "Original Size: " << input_data.size() << " bytes"
              << std::endl;

    // 2. 调用纯净版算法进行压缩
    std::vector<uint8_t> compressed_data = LZ77::compress(input_data);

    std::cout << "Compressed Size: " << compressed_data.size() << " bytes"
              << std::endl;

    // 计算并打印压缩率
    double ratio =
        static_cast<double>(input_data.size() - compressed_data.size()) /
        input_data.size() * 100.0;
    std::cout << "Compression Ratio: " << ratio << "%" << std::endl;

    // 3. 将压缩后的结果保存到本地磁盘
    std::string output_filename =
        "../../tests/data/test_page.lz77";  // 自定义一个酷炫的后缀名
    if (writeToFile(output_filename, compressed_data)) {
        std::cout << "\n🎉 Success! Compressed file generated at: "
                  << output_filename << std::endl;
    }

    // 4. 解压缩
    filename = "../../tests/data/test_decompressed.html";
    std::vector<uint8_t> decompressed_data = LZ77::decompress(compressed_data);
    if (writeToFile(filename, decompressed_data)) {
        std::cout << "\n🎉 Success! Decompressed file generated at: "
                  << filename << std::endl;
    }

    return 0;
}

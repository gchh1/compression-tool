#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

#include "api.hpp"

using namespace compressor::api;

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
    std::vector<AlgorithmID> chain = {AlgorithmID::Deflate};

    std::cout << "--- Deflate (Pipeline) File Compression Test ---" << std::endl;

    // 1. 读取网页文件
    std::string filename = "../../tests/data/cmu445.html";
    std::vector<uint8_t> input_data = readFile(filename);

    if (input_data.empty()) {
        std::cerr << "Please check if the file exists!" << std::endl;
        return 1;
    }

    std::cout << "Original Size: " << input_data.size() << " bytes"
              << std::endl;

    // 2. 调用 Pipeline API 进行压缩
    CompressResult result = compress(input_data, chain);

    std::cout << "Compressed Size: " << result.compressed_size
              << " bytes" << std::endl;

    // 计算并打印压缩率
    std::cout << "Compression Ratio: " << result.compression_ratio
              << std::endl;

    // print time
    std::cout << "Compress Time: " << result.time_ms << "ms"
              << std::endl;

    // 3. 将压缩后的结果保存到本地磁盘
    std::string output_filename =
        "../../tests/data/cmu445.Deflate";
    if (writeToFile(output_filename, result.data)) {
        std::cout << "\nSuccess! Compressed file generated at: "
                  << output_filename << std::endl;
    }

    // 4. 解压缩
    std::vector<AlgorithmID> decomp_chain = {AlgorithmID::Inflate};
    filename = "../../tests/data/cmu445_decompressed.html";
    CompressResult decompressed_data =
        decompress(result.data, decomp_chain);
    if (writeToFile(filename, decompressed_data.data)) {
        std::cout << "\nSuccess! Decompressed file generated at: "
                  << filename << std::endl;
    }

    return 0;
}

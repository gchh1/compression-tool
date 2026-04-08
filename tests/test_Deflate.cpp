#include "DeflateCompressor.hpp"

#include <iostream>
#include <vector>

int main() {
    compressor::core::DeflateCompressor compressor;

    // 测试数据
    std::vector<uint8_t> original_data = {'H', 'e', 'l', 'l', 'o', ',', ' ', 'W', 'o', 'r', 'l', 'd', '!'};

    // 压缩
    auto compress_result = compressor.compress(original_data);
    std::cout << "Original size: " << compress_result.original_size << std::endl;
    std::cout << "Compressed size: " << compress_result.compressed_size << std::endl;
    std::cout << "Compression ratio: " << compress_result.compression_ratio << std::endl;
    std::cout << "Time: " << compress_result.time_ms << " ms" << std::endl;

    // 解压
    auto decompress_result = compressor.decompress(compress_result.data);
    std::cout << "Decompressed size: " << decompress_result.compressed_size << std::endl;

    // 检查是否相同
    if (original_data == decompress_result.data) {
        std::cout << "Compression and decompression successful!" << std::endl;
    } else {
        std::cout << "Error: Data mismatch!" << std::endl;
    }

    return 0;
}

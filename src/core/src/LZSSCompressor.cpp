#include "LZSSCompressor.hpp"

#include <chrono>
#include <cstdint>
#include <locale>
#include <vector>

#include "LZSS.hpp"
#include "compressor.hpp"

namespace compressor {
namespace core {
CompressResult LZSSCompressor::compress(const std::vector<uint8_t>& data) {
    CompressResult result;

    auto start_time = std::chrono::high_resolution_clock::now();
    result.data = algorithm::LZSS::compress(data);
    auto end_time = std::chrono::high_resolution_clock::now();
    result.original_size = data.size();
    result.compressed_size = result.data.size();
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
    result.time_ms = elapsed.count();
    result.compression_ratio =
        static_cast<double>(result.compressed_size) / result.original_size;

    return result;
}

CompressResult LZSSCompressor::decompress(const std::vector<uint8_t>& data) {
    CompressResult result;

    auto start_time = std::chrono::high_resolution_clock::now();
    result.data = algorithm::LZSS::decompress(data);
    auto end_time = std::chrono::high_resolution_clock::now();
    result.original_size = data.size();
    result.compressed_size = result.data.size();
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
    result.time_ms = elapsed.count();
    result.compression_ratio =
        static_cast<double>(result.compressed_size) / result.original_size;

    return result;
}

}  // namespace core

}  // namespace compressor
#include "LZSSCompressor.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

#include "LZSS.hpp"
#include "ICompressor.hpp"

namespace compressor {
namespace core {
auto LZSSCompressor::compress(std::vector<uint8_t> data) -> CompressorResult {
    CompressorResult result;

    auto start_time = std::chrono::high_resolution_clock::now();
    size_t actual_min_match = min_match_length_;
    if (actual_min_match == 0) {
        actual_min_match = 3;
    }
    result.data = algorithm::LZSS::compress(data, dictionary_buffer_size_, actual_min_match);
    auto end_time = std::chrono::high_resolution_clock::now();
    result.original_size = data.size();
    result.compressed_size = result.data.size();
    result.compression_ratio =
        static_cast<double>(result.compressed_size) / result.original_size;
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
    result.time_ms = elapsed.count();

    return result;
}

auto LZSSCompressor::decompress(std::vector<uint8_t> data) -> CompressorResult {
    CompressorResult result;

    auto start_time = std::chrono::high_resolution_clock::now();
    size_t actual_min_match = min_match_length_;
    if (actual_min_match == 0) {
        actual_min_match = 3;
    }
    result.data = algorithm::LZSS::decompress(data, actual_min_match);
    auto end_time = std::chrono::high_resolution_clock::now();
    result.original_size = data.size();
    result.compressed_size = result.data.size();
    result.compression_ratio =
        static_cast<double>(result.compressed_size) / result.original_size;
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
    result.time_ms = elapsed.count();

    return result;
}

}  // namespace core

}  // namespace compressor
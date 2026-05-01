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
    result.data = algorithm::LZSS::compress(data);
    auto end_time = std::chrono::high_resolution_clock::now();
    result.original_size = data.size();
    result.compressed_size = result.data.size();
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
    result.time_ms = elapsed.count();

    return result;
}

auto LZSSCompressor::decompress(std::vector<uint8_t> data) -> CompressorResult {
    CompressorResult result;

    auto start_time = std::chrono::high_resolution_clock::now();
    result.data = algorithm::LZSS::decompress(data);
    auto end_time = std::chrono::high_resolution_clock::now();
    result.original_size = data.size();
    result.compressed_size = result.data.size();
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
    result.time_ms = elapsed.count();

    return result;
}

}  // namespace core

}  // namespace compressor
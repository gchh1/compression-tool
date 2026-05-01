#include "LZMineCompressor.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

namespace compressor {
namespace core {

auto LZMineCompressor::compress(std::vector<uint8_t> data) -> CompressorResult {
    CompressorResult result;

    auto start_time = std::chrono::high_resolution_clock::now();
    result.data = algorithm::LZMine(2, 2).compress(data, 4096, 256);
    auto end_time = std::chrono::high_resolution_clock::now();

    result.original_size = data.size();
    result.compressed_size = result.data.size();
    if (result.original_size > 0) {
        result.compression_ratio =
            static_cast<double>(result.compressed_size) / result.original_size;
    }
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
    result.time_ms = elapsed.count();
    result.success = true;

    return result;
}

auto LZMineCompressor::decompress(std::vector<uint8_t> data)
    -> CompressorResult {
    CompressorResult result;

    auto start_time = std::chrono::high_resolution_clock::now();
    result.data = algorithm::LZMine(2, 2).decompress(data);
    auto end_time = std::chrono::high_resolution_clock::now();

    result.original_size = data.size();
    result.compressed_size = result.data.size();
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
    result.time_ms = elapsed.count();
    result.success = true;

    return result;
}

} // namespace core
} // namespace compressor

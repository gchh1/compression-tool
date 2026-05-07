#include "CrazyFlateCompressor.hpp"

#include <chrono>
#include <vector>

#include "CrazyFlate.hpp"

namespace compressor {
namespace core {

auto CrazyFlateCompressor::compress(std::vector<uint8_t> data) -> CompressorResult {
    CompressorResult result;

    auto start_time = std::chrono::high_resolution_clock::now();
    result.data = algorithm::CrazyFlate::compress(data, search_size_, lookahead_size_, min_match_, dp_depth_, max_chain_length_);
    auto end_time = std::chrono::high_resolution_clock::now();
    result.original_size = data.size();
    result.compressed_size = result.data.size();
    result.compression_ratio =
        static_cast<double>(result.compressed_size) / result.original_size;
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
    result.time_ms = elapsed.count();

    return result;
}

auto CrazyFlateCompressor::decompress(std::vector<uint8_t> data) -> CompressorResult {
    CompressorResult result;

    auto start_time = std::chrono::high_resolution_clock::now();
    result.data = algorithm::CrazyFlate::decompress(data);
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

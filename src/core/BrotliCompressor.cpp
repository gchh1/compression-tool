#include "BrotliCompressor.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

#include "Brotli.hpp"

namespace compressor::core {

auto BrotliCompressor::compress(std::vector<uint8_t> data) -> CompressorResult {
    CompressorResult result;
    const auto start_time = std::chrono::high_resolution_clock::now();

    algorithm::BrotliParams params;
    params.window_size = window_size_;
    params.min_match = min_match_ == 0 ? 3 : min_match_;
    params.max_chain_length = max_chain_length_;

    result.data = algorithm::brotli_encode(data, params);

    const auto end_time = std::chrono::high_resolution_clock::now();
    result.original_size = data.size();
    result.compressed_size = result.data.size();
    if (result.original_size > 0) {
        result.compression_ratio =
            static_cast<double>(result.compressed_size) / result.original_size;
    }
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
    result.time_ms = elapsed.count();
    result.success = !result.data.empty() || data.empty();

    return result;
}

auto BrotliCompressor::decompress(std::vector<uint8_t> data) -> CompressorResult {
    CompressorResult result;
    const auto start_time = std::chrono::high_resolution_clock::now();

    result.data = algorithm::brotli_decode(data);

    const auto end_time = std::chrono::high_resolution_clock::now();
    result.original_size = data.size();
    result.compressed_size = result.data.size();
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
    result.time_ms = elapsed.count();
    result.success = !result.data.empty() || data.empty();

    return result;
}

}  // namespace compressor::core

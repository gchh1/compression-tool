#include "LZDPCompressor.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

namespace compressor {
namespace core {

auto LZDPCompressor::compress(std::vector<uint8_t> data) -> CompressorResult {
    CompressorResult result;

    auto start_time = std::chrono::high_resolution_clock::now();
    algorithm::LZDP lzdp;
    lzdp.set_min_match(min_match_);
    lzdp.set_use_flag_encoding(use_flag_encoding_);
    lzdp.set_match_engine(match_engine_);
    lzdp.autoBitWidth(search_size_, lookahead_size_);
    if (dp_range_ > 1) {
        result.data = lzdp.compress_dp(data, search_size_, lookahead_size_, dp_range_);
    } else {
        result.data = lzdp.compress(data, search_size_, lookahead_size_);
    }
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

auto LZDPCompressor::decompress(std::vector<uint8_t> data)
    -> CompressorResult {
    CompressorResult result;

    auto start_time = std::chrono::high_resolution_clock::now();
    result.data = algorithm::LZDP().decompress(data);
    auto end_time = std::chrono::high_resolution_clock::now();

    result.original_size = data.size();
    result.compressed_size = result.data.size();
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
    result.time_ms = elapsed.count();
    result.success = true;

    return result;
}

algorithm::LZDP::DPVisualization LZDPCompressor::get_dp_visualization(
    const std::vector<uint8_t>& data, size_t range) {
    if (range == 0) range = dp_range_;
    auto start_time = std::chrono::high_resolution_clock::now();
    algorithm::LZDP lzdp;
    lzdp.set_min_match(min_match_);
    lzdp.set_use_flag_encoding(use_flag_encoding_);
    lzdp.set_match_engine(match_engine_);
    lzdp.autoBitWidth(search_size_, lookahead_size_);
    if (data.size() > 65536) {
        algorithm::LZDP::DPVisualization empty_viz;
        empty_viz.input_length = data.size();
        empty_viz.search_size = search_size_;
        empty_viz.lookahead_size = lookahead_size_;
        return empty_viz;
    }
    return lzdp.get_dp_visualization(data, search_size_, lookahead_size_, range);
}

} // namespace core
} // namespace compressor
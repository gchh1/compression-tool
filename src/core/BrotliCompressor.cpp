#include "BrotliCompressor.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

#include "Brotli.hpp"

namespace compressor {
namespace core {

auto BrotliCompressor::compress(std::vector<uint8_t> data) -> CompressorResult {
    CompressorResult result;
    auto start_time = std::chrono::high_resolution_clock::now();

    algorithm::BrotliCompress brotli(window_size_, min_match_ == 0 ? 3 : min_match_, max_chain_length_);
    std::vector<uint8_t> out(data.size() + 1024);
    auto status = brotli.process(data, out, true);
    out.resize(status.bytes_produced);

    result.data = std::move(out);

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

auto BrotliCompressor::decompress(std::vector<uint8_t> data) -> CompressorResult {
    CompressorResult result;
    auto start_time = std::chrono::high_resolution_clock::now();

    algorithm::BrotliDecompress decompress;
    std::vector<uint8_t> out(std::max(data.size() * 4 + 65536, size_t(2097152)));
    auto status = decompress.process(data, out, true);
    out.resize(status.bytes_produced);

    result.data = std::move(out);

    auto end_time = std::chrono::high_resolution_clock::now();
    result.original_size = data.size();
    result.compressed_size = result.data.size();
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
    result.time_ms = elapsed.count();
    result.success = true;

    return result;
}

}
}
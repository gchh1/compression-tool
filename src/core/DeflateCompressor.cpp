#include "DeflateCompressor.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

#include "Deflate.hpp"
#include "ICompressor.hpp"

namespace compressor {
namespace core {

auto DeflateCompressor::compress(std::vector<uint8_t> original_data)
    -> CompressorResult {
    CompressorResult result;
    result.original_size = original_data.size();

    auto start_time = std::chrono::high_resolution_clock::now();

    algorithm::Deflate deflate;
    deflate.reset();

    size_t out_capacity = original_data.size() + 1024;
    if (out_capacity < 4096) out_capacity = 4096;

    std::vector<uint8_t> out(out_capacity);
    auto status = deflate.process(original_data, out, true);

    result.data.resize(status.bytes_produced);
    if (status.bytes_produced > 0) {
        std::copy_n(out.begin(), status.bytes_produced, result.data.begin());
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;

    result.compressed_size = result.data.size();
    result.time_ms = elapsed.count();

    if (result.original_size > 0) {
        result.compression_ratio =
            static_cast<double>(result.compressed_size) / result.original_size;
    }
    result.success = true;

    return result;
}

auto DeflateCompressor::decompress(std::vector<uint8_t> compressed_data)
    -> CompressorResult {
    CompressorResult result;
    result.compressed_size = compressed_data.size();
    result.success = false;
    result.error_message = "Deflate decompress not yet available (Inflate has compile issues)";
    return result;
}

} // namespace core
} // namespace compressor

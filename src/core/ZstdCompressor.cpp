#include "ZstdCompressor.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <vector>

#include "Zstd.hpp"

namespace compressor {
namespace core {

auto ZstdCompressor::compress(std::vector<uint8_t> data) -> CompressorResult {
    CompressorResult result;
    auto start_time = std::chrono::high_resolution_clock::now();

    auto compressed = algorithm::ZstdCompress::compress(data, compression_level_);
    result.data = std::move(compressed);

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

auto ZstdCompressor::decompress(std::vector<uint8_t> data) -> CompressorResult {
    CompressorResult result;
    auto start_time = std::chrono::high_resolution_clock::now();

    algorithm::ZstdDecompress decompressor;
    auto decompressed = decompressor.decompress(data);
    result.data = std::move(decompressed);

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
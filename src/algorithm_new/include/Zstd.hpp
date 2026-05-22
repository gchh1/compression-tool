#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace compressor::algorithm {

auto zstd_compress(const std::vector<uint8_t>& data, int compression_level = 3)
    -> std::vector<uint8_t>;

auto zstd_decompress(const std::vector<uint8_t>& data) -> std::vector<uint8_t>;

}  // namespace compressor::algorithm
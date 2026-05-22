#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace compressor::algorithm {

struct BrotliParams {
    size_t window_size{65536};
    size_t min_match{4};
    size_t max_chain_length{256};
};

auto brotli_encode(const std::vector<uint8_t>& data,
                   const BrotliParams& params = {}) -> std::vector<uint8_t>;
auto brotli_decode(const std::vector<uint8_t>& data) -> std::vector<uint8_t>;

}  // namespace compressor::algorithm
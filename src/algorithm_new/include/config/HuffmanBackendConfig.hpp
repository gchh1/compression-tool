#pragma once

#include <cstddef>
#include <cstdint>

namespace compressor::algorithm::config {

struct HuffmanBackendConfig {
    bool use_3hfmtree;
    uint8_t huffman_offset_bitwidth;
    uint8_t huffman_length_bitwidth;

    HuffmanBackendConfig(
        bool use3hfm = false,
        uint8_t off_chunk_bits = 8,
        uint8_t len_chunk_bits = 8)
        : use_3hfmtree(use3hfm),
          huffman_offset_bitwidth(off_chunk_bits),
          huffman_length_bitwidth(len_chunk_bits) {}
};

}  // namespace compressor::algorithm::config
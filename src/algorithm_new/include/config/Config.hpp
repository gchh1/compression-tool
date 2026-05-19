#pragma once

#include <cstddef>
#include <cstdint>

#include "Models.hpp"

namespace compressor::algorithm::config {

// --- Shared config bricks (§1.11) ---

struct Lz77WindowConfig {
    size_t search_size;
    size_t look_size;
    size_t min_match_len;

    Lz77WindowConfig(
        size_t sw = 4095,
        size_t lw = 255,
        size_t mml = 0)
        : search_size(sw), look_size(lw), min_match_len(mml) {}
};

struct DpMatcherConfig {
    size_t dp_top;
    models::MatchEngine match_engine;

    DpMatcherConfig(
        size_t dp = 3,
        models::MatchEngine me = models::MatchEngine::HashChain)
        : dp_top(dp), match_engine(me) {}
};

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
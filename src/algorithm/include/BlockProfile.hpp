#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace compressor::algorithm {

/// Per-block statistics captured during Deflate compression.
struct BlockInfo {
    size_t block_index{0};
    size_t literal_count{0};       // number of literal tokens
    size_t match_count{0};         // number of LZ match tokens
    size_t ll_tree_bits{0};        // bits for literal/length Huffman tree
    size_t dist_tree_bits{0};      // bits for distance Huffman tree
    size_t output_bytes{0};        // uncompressed bytes this block covers
    std::vector<uint8_t> ll_code_lengths;    // 286 canonical code lengths
    std::vector<uint8_t> dist_code_lengths;  // 30 canonical code lengths
};

struct BlockProfile {
    std::vector<BlockInfo> blocks;
};

}  // namespace compressor::algorithm

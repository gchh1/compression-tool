#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace test_bridge {

std::vector<uint8_t> old_dpflate_compress(
    const std::vector<uint8_t>& data,
    size_t search_size,
    size_t lookahead_size,
    size_t min_match,
    size_t dp_top,
    size_t dp_sub_match_max,
    bool use_3hfmtree,
    size_t huffman_chunk_bits);

}
#pragma once

#include <cstddef>

namespace compressor::algorithm::config {

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

}  // namespace compressor::algorithm::config
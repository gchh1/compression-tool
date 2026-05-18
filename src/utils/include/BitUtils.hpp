#pragma once

#include <cstddef>
#include <cstdint>

namespace compressor::utils {

/// Minimum number of bits needed to represent `max_val`.
/// Returns 1 for 0 (at least 1 bit to distinguish "no value").
inline auto calcBitWidth(size_t max_val) -> uint8_t {
    if (max_val == 0) return 1;
    uint8_t bits = 0;
    while (max_val > 0) {
        ++bits;
        max_val >>= 1;
    }
    return bits;
}

}  // namespace compressor::utils
#pragma once

#include <cstddef>
#include <cstdint>

namespace compressor::algorithm::utils {

inline size_t ceilDiv(size_t numerator, size_t divisor) {
    return (numerator + divisor - 1) / divisor;// 上取整
}

/// 表示 [0, windowSize] 所需的最少位数（windowSize 为 0 时返回 1）
template <typename T>
inline T calcBitWidth(T windowSize) {
    if (windowSize == 0) {
        return T{1};
    }
    uint32_t bitwidth = 0;
    uint32_t v = static_cast<uint32_t>(windowSize);
    while (v > 0) {
        ++bitwidth;
        v >>= 1;
    }
    return static_cast<T>(bitwidth);
}

/// ``min_match_len == 0`` 时按旧 ``algorithm/LZDP.hpp::get_min_match`` 规则自动计算：
/// ``(offset_bits + length_bits) / 8 + 1``（匹配编码至少占一字节且需有实际匹配长度）。
inline uint32_t getMinMatch(uint32_t offsetBits, uint32_t lengthBits) {
    return (offsetBits + lengthBits) / 8 + 1;
}

inline size_t effectiveMinMatchLen(size_t configured, uint32_t offsetBits, uint32_t lengthBits) {
    return configured != 0 ? configured : static_cast<size_t>(getMinMatch(offsetBits, lengthBits));
}

}  // namespace compressor::algorithm::utils

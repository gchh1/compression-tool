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

/// 匹配编码总位宽 Ob+Lb 对 8 严格上取整（字节数）
inline uint32_t getMinMatch(uint32_t offsetBits, uint32_t lengthBits) {
    return ceilDiv(offsetBits + lengthBits, 8);
}

}  // namespace compressor::algorithm::utils

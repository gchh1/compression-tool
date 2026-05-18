#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Streaming.hpp"

namespace compressor::algorithm {

/// 绝对文件下标视图：`input[abs_pos]`，用于 VB 滑动窗 + 全局 DP 数组。
template <typename Container>
struct AbsoluteByteView {
    const Container& data;
    size_t base{0};

    size_t window_size() const { return data.size(); }

    uint8_t operator[](size_t abs_pos) const { return data[abs_pos - base]; }
};

/// 匹配器用相对下标切片（HashChain / KMP 的 `begin[i]`）。
template <typename Container>
struct RelativeByteSlice {
    const Container& data;
    size_t base{0};

    uint8_t operator[](size_t i) const { return data[base + i]; }
};

struct VectorByteInput {
    const std::vector<uint8_t>& data;
    size_t base{0};
    size_t window_size() const { return data.size(); }
    uint8_t operator[](size_t abs_pos) const { return data[abs_pos]; }
};

struct VbByteInput {
    const streaming::VirtualBuffer<std::vector<uint8_t>>& data;
    size_t base{0};
    size_t window_size() const { return data.size(); }
    uint8_t operator[](size_t abs_pos) const { return data[abs_pos - base]; }
};

}  // namespace compressor::algorithm

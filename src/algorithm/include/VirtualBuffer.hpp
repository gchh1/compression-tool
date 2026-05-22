#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace compressor {
namespace algorithm {

/// VirtualBuffer — 环形槽位零拷贝拼接
///
/// 将 N 个独立内存段映射为连续逻辑地址空间，仅暴露 operator[] 容器语义。
/// 槽位通过 head_ 旋转管理：逻辑视图为 slots_[head_] | slots_[(head_+1)%N] | ...
///
/// slide() 释放最旧槽位并旋转 head_，append() 填充最新槽位——均零拷贝。
///
/// 典型用法（三分块）：
///   VirtualBuffer<3> vb;
///   vb.append(search_win);   // slot 0 = prev
///   vb.append(current);      // slot 1 = current
///   vb.append(lookahead);    // slot 2 = new
///   uint8_t byte = vb[i];    // 0..len-1 自动路由
///   vb.slide();              // 释放 prev，旋转
///   vb.append(next_chunk);   // 填入新数据
template <size_t N = 3>
class VirtualBuffer {
public:
    VirtualBuffer() {
        for (auto& s : slots_) s = {};
    }

    /// 从分段列表构造（用于非流式场景，如 dp_core 的单段输入）
    explicit VirtualBuffer(std::vector<std::span<const uint8_t>> segs) {
        for (auto& s : slots_) s = {};
        size_t n = segs.size() < N ? segs.size() : N;
        for (size_t i = 0; i < n; ++i) {
            slots_[i] = segs[i];
        }
        recalc_total();
    }

    size_t size() const { return total_size_; }

    /// 随机访问 — O(N) 线性路由，N ≤ 3 退化为常数
    uint8_t operator[](size_t i) const {
        for (size_t s = 0; s < N; ++s) {
            const auto& slot = slots_[(head_ + s) % N];
            if (i < slot.size()) {
                return slot[i];
            }
            i -= slot.size();
        }
        throw std::out_of_range("VirtualBuffer index out of range");
    }

    /// 旋转释放最旧槽位（prev），零拷贝
    void slide() {
        slots_[head_] = {};
        head_ = (head_ + 1) % N;
        recalc_total();
    }

    /// 填充最新槽位（new），零拷贝
    void append(std::span<const uint8_t> data) {
        size_t new_slot = (head_ + N - 1) % N;
        slots_[new_slot] = data;
        recalc_total();
    }

    /// Minimal random-access iterator for KMP / algorithmic use.
    /// Supports operator[] and operator+ so it works as a template Iter.
    struct ConstIterator {
        const VirtualBuffer* vb;
        size_t pos;

        uint8_t operator[](size_t i) const { return (*vb)[pos + i]; }
        ConstIterator operator+(size_t n) const { return {vb, pos + n}; }
    };

    ConstIterator begin() const { return {this, 0}; }

private:
    void recalc_total() {
        total_size_ = 0;
        for (size_t s = 0; s < N; ++s) {
            total_size_ += slots_[(head_ + s) % N].size();
        }
    }

    std::span<const uint8_t> slots_[N];
    size_t head_{0};
    size_t total_size_{0};
};

} // namespace algorithm
} // namespace compressor
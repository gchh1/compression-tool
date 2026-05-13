#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <stdexcept>
#include <vector>

namespace compressor {
namespace algorithm {

/// VirtualBuffer — 零拷贝"假"拼接
///
/// 接收一个"装容器的容器"（std::vector<std::span<const uint8_t>>），
/// 将多个独立内存段映射为连续逻辑地址空间。
/// 支持 operator[] 和随机访问迭代器，可替代 std::vector<uint8_t>
/// 用于 DP Core 等需要连续视图但不想物理拼接的场景。
///
/// 典型用法：
///   std::vector<std::span<const uint8_t>> segments;
///   segments.push_back(search_win);
///   segments.push_back(current);
///   segments.push_back(lookahead);
///   VirtualBuffer vb(std::move(segments));
///   uint8_t byte = vb[i];  // 自动路由到对应分段
class VirtualBuffer {
public:
    VirtualBuffer() : total_size_(0) {}

    /// 从"装容器的容器"构造 — 接受任意数量的内存段
    explicit VirtualBuffer(std::vector<std::span<const uint8_t>> segs)
        : segments_(std::move(segs)), total_size_(0) {
        for (auto& s : segments_) {
            total_size_ += s.size();
        }
    }

    /// 总大小（所有分段之和）
    size_t size() const { return total_size_; }

    /// 随机访问 — 自动路由到对应分段
    uint8_t operator[](size_t i) const {
        for (auto& s : segments_) {
            if (i < s.size()) {
                return s[i];
            }
            i -= s.size();
        }
        throw std::out_of_range("VirtualBuffer index out of range");
    }

    // ─── 自定义随机访问迭代器 ──────────────────────────────
    class const_iterator {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type        = uint8_t;
        using difference_type   = ptrdiff_t;
        using pointer           = const uint8_t*;
        using reference         = uint8_t;

        const_iterator() : vb_(nullptr), idx_(0) {}

        reference operator*() const { return (*vb_)[idx_]; }
        reference operator[](difference_type n) const { return (*vb_)[idx_ + n]; }

        const_iterator& operator++() { ++idx_; return *this; }
        const_iterator operator++(int) { auto tmp = *this; ++idx_; return tmp; }
        const_iterator& operator--() { --idx_; return *this; }
        const_iterator operator--(int) { auto tmp = *this; --idx_; return tmp; }

        const_iterator& operator+=(difference_type n) { idx_ += n; return *this; }
        const_iterator& operator-=(difference_type n) { idx_ -= n; return *this; }

        friend const_iterator operator+(const const_iterator& it, difference_type n) {
            return const_iterator(it.vb_, it.idx_ + n);
        }
        friend const_iterator operator+(difference_type n, const const_iterator& it) {
            return const_iterator(it.vb_, it.idx_ + n);
        }
        friend const_iterator operator-(const const_iterator& it, difference_type n) {
            return const_iterator(it.vb_, it.idx_ - n);
        }
        friend difference_type operator-(const const_iterator& a, const const_iterator& b) {
            return static_cast<difference_type>(a.idx_) - static_cast<difference_type>(b.idx_);
        }

        friend bool operator==(const const_iterator& a, const const_iterator& b) { return a.idx_ == b.idx_; }
        friend bool operator!=(const const_iterator& a, const const_iterator& b) { return a.idx_ != b.idx_; }
        friend bool operator<(const const_iterator& a, const const_iterator& b) { return a.idx_ < b.idx_; }
        friend bool operator<=(const const_iterator& a, const const_iterator& b) { return a.idx_ <= b.idx_; }
        friend bool operator>(const const_iterator& a, const const_iterator& b) { return a.idx_ > b.idx_; }
        friend bool operator>=(const const_iterator& a, const const_iterator& b) { return a.idx_ >= b.idx_; }

    private:
        friend class VirtualBuffer;
        const_iterator(const VirtualBuffer* vb, size_t idx) : vb_(vb), idx_(idx) {}

        const VirtualBuffer* vb_;
        size_t idx_;
    };

    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end()   const { return const_iterator(this, total_size_); }

private:
    std::vector<std::span<const uint8_t>> segments_;
    size_t total_size_;
};

} // namespace algorithm
} // namespace compressor
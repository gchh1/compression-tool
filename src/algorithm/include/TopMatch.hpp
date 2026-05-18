#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace compressor::algorithm {

/// 按匹配长度维护 Top-K（降序：``entries_[0]`` 最长）。与 ``algorithm_new`` ``models::TopMatch`` 同构。
struct TopMatchEntry {
    uint16_t offset{0};
    uint16_t length{0};
};

class TopMatch {
    size_t capacity_;
    std::vector<TopMatchEntry> entries_;

public:
    explicit TopMatch(size_t dp_top) : capacity_(dp_top) {}

    void insert(uint16_t offset, uint16_t length) {
        if (capacity_ == 0 || offset == 0) {
            return;
        }
        size_t lo = 0;
        size_t hi = entries_.size();
        while (lo < hi) {
            const size_t mid = lo + (hi - lo) / 2;
            if (entries_[mid].length >= length) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        entries_.insert(entries_.begin() + static_cast<std::ptrdiff_t>(lo),
                        TopMatchEntry{offset, length});
        if (entries_.size() > capacity_) {
            entries_.pop_back();
        }
    }

    [[nodiscard]] bool empty() const { return entries_.empty(); }
    [[nodiscard]] size_t size() const { return entries_.size(); }
    [[nodiscard]] const std::vector<TopMatchEntry>& entries() const { return entries_; }
};

}  // namespace compressor::algorithm

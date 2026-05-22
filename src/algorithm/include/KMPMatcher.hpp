#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace compressor {
namespace algorithm {

struct KMPMatchResult {
    size_t offset;
    size_t length;
};

namespace kmp_detail {

static constexpr size_t MAX_RING_N = 16;

struct MatchRing {
    KMPMatchResult buffer[MAX_RING_N];
    size_t capacity;
    size_t count;
    size_t cursor;

    void init(size_t n) {
        capacity = (n > MAX_RING_N) ? MAX_RING_N : n;
        count = 0;
        cursor = 0;
    }

    bool isFull() const { return count == capacity; }

    void tryInsert(const KMPMatchResult& t) {
        if (t.offset == 0) return;
        if (!isFull()) {
            buffer[cursor % capacity] = t;
            cursor++;
            count++;
            return;
        }
        size_t start = cursor % capacity;
        for (size_t i = 0; i < capacity; i++) {
            size_t idx = (start + i) % capacity;
            if (t.length > buffer[idx].length) {
                buffer[idx] = t;
                cursor = cursor + i + 1;
                return;
            }
        }
        cursor += capacity;
    }

    void toVector(std::vector<KMPMatchResult>& out) {
        out.clear();
        if (count == 0) return;
        size_t actual = (count < capacity) ? count : capacity;
        for (size_t i = 0; i < actual; i++) {
            out.push_back(buffer[i]);
        }
        std::sort(out.begin(), out.end(),
            [](const KMPMatchResult& a, const KMPMatchResult& b) {
                return a.length > b.length;
            });
    }
};

template <typename Iter>
std::vector<size_t> buildNext(Iter pattern, size_t len) {
    std::vector<size_t> next(len, 0);
    if (len == 0) return next;
    size_t j = 0;
    for (size_t i = 1; i < len; i++) {
        while (pattern[i] != pattern[j] && j != 0) {
            j = next[j - 1];
        }
        if (pattern[i] == pattern[j]) j++;
        next[i] = j;
    }
    return next;
}

}  // namespace kmp_detail

template <typename Iter>
std::vector<KMPMatchResult> kmpSearch(
    Iter search_begin, size_t search_len,
    Iter lookahead_begin, size_t lookahead_len,
    size_t dp_top = 3,
    size_t min_match = 3) {

    if (lookahead_len < min_match) {
        return {};
    }

    kmp_detail::MatchRing ring;
    ring.init(dp_top);

    auto next = kmp_detail::buildNext(lookahead_begin, lookahead_len);
    size_t j = 0;
    size_t maxlen = 0;
    size_t mark_idx = 0;

    size_t total_len = search_len + lookahead_len - 1;

    for (size_t i = 0; i < total_len; i++) {
        auto current_char = (i < search_len) ? search_begin[i] : lookahead_begin[i - search_len];
        while (current_char != lookahead_begin[j] && j != 0) {
            j = next[j - 1];
        }
        if (current_char == lookahead_begin[j]) { j++; }

        if (j >= lookahead_len) {
            size_t match_len = lookahead_len;
            if (match_len >= min_match) {
                // If i >= search_len, match start is in search window iff i - j + 1 < search_len
                // which means search_len - i + j - 1 > 0
                size_t dist = search_len - i + match_len - 1;
                if (dist > 0) {
                    if (dp_top > 1) {
                        ring.tryInsert({dist, match_len});
                    } else {
                        return {{dist, match_len}};
                    }
                }
            }
            j = (lookahead_len > 1) ? next[lookahead_len - 1] : 0;
        } else if (j >= min_match) {
            size_t dist = search_len - i + j - 1;
            if (dist > 0) {
                if (dp_top > 1) {
                    ring.tryInsert({dist, j});
                } else if (dp_top == 1 && j >= maxlen) {
                    maxlen = j;
                    mark_idx = i;
                }
            }
        }
    }

    if (dp_top == 1) {
        if (maxlen >= min_match) {
            size_t dist = search_len - mark_idx + maxlen - 1;
            if (dist > 0) {
                return {{dist, maxlen}};
            }
        }
        return {};
    }

    std::vector<KMPMatchResult> result;
    ring.toVector(result);
    return result;
}

}  // namespace algorithm
}  // namespace compressor
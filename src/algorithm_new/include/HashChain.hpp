#pragma once

#include <cstdint>
#include <vector>

#include "LZencoding.hpp"
#include "Models.hpp"

namespace compressor::algorithm::LZMatcher {

template <typename Iter>
std::vector<Triple> hashChainSearch(
    Iter search_begin,
    uint32_t search_len,
    Iter lookahead_begin,
    uint32_t lookahead_len,
    uint8_t dp_top,
    uint32_t min_match_len)
{
    if (lookahead_len < min_match_len || dp_top == 0 || search_len == 0) {
        return {};
    }

    const uint32_t pos = search_len;
    const uint32_t total = search_len + lookahead_len;

    auto at = [&](uint32_t i) -> uint8_t {
        return (i < search_len) ? static_cast<uint8_t>(search_begin[i])
                                : static_cast<uint8_t>(lookahead_begin[i - search_len]);
    };

    uint32_t bucket_count = 1;
    while (bucket_count <= search_len) {
        bucket_count <<= 1;
    }
    const uint32_t hash_mask = bucket_count - 1;

    std::vector<uint32_t> head(bucket_count, UINT32_MAX);
    std::vector<uint32_t> prev(total, UINT32_MAX);

    for (uint32_t p = 0; p + 2 < total; ++p) {
        const uint8_t a = at(p), b = at(p + 1), c = at(p + 2);
        const uint32_t slot =
            ((uint32_t{a} << 10) ^ (uint32_t{b} << 5) ^ c) & hash_mask;
        prev[p] = head[slot];
        head[slot] = p;
    }

    models::TopMatch matches(dp_top);

    uint32_t match_pos = (pos + 2 < total) ? prev[pos] : UINT32_MAX;
    uint32_t chain_length = static_cast<uint32_t>(dp_top) * 8;

    while (match_pos != UINT32_MAX && chain_length-- > 0) {
        const uint32_t offset = pos - match_pos;
        if (offset > search_len || offset == 0) {
            break;
        }

        uint32_t match_len = 0;
        while (match_len < lookahead_len &&
               at(pos + match_len) == at(match_pos + match_len)) {
            ++match_len;
        }

        if (match_len >= min_match_len) {
            matches.insert(Triple(offset, match_len));
        }
        match_pos = prev[match_pos];
    }

    if (matches.isEmpty()) {
        return {Triple(0, 1, lookahead_begin[0])};
    }
    return matches.getTriples();
}

}  // namespace compressor::algorithm::LZMatcher

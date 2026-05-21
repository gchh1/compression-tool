#pragma once

/// §1.14 匹配引擎砖块：HashChain / KMP 共用「按位置返回 Triple 容器」接口；
/// ``dp_top == 1`` 时容器语义为贪心（取最长一条），``dp_top >= 1`` 时供 LZDP/DPFlate DP relax。

#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "ByteView.hpp"
#include "HashChain.hpp"
#include "KMP.hpp"
#include "LZencoding.hpp"
#include "Models.hpp"
#include "Utils.hpp"
#include "config/Config.hpp"
#include "StreamingCancel.hpp"

namespace compressor::algorithm::LZMatcher {

template <typename SearchIter, typename LookIter>
inline std::vector<Triple> matchAtPosition(
    models::MatchEngine engine,
    SearchIter search_begin,
    uint32_t search_len,
    LookIter lookahead_begin,
    uint32_t lookahead_len,
    uint8_t dp_top,
    uint32_t min_match_len) {
    if (engine == models::MatchEngine::KMP) {
        return kmpSearch(
            search_begin,
            search_len,
            lookahead_begin,
            lookahead_len,
            dp_top,
            min_match_len);
    }
    return hashChainSearch(
        search_begin,
        search_len,
        lookahead_begin,
        lookahead_len,
        dp_top,
        min_match_len);
}

/// 整段输入上的贪心 LZ77：每步 ``matchAtPosition(..., dp_top=1)``，与 KMP 设计一致。
inline std::vector<Triple> greedyWholeInput(
    const std::vector<uint8_t>& input,
    const config::Lz77WindowConfig& window,
    uint8_t offset_bits,
    uint8_t length_bits,
    models::MatchEngine engine = models::MatchEngine::HashChain) {
    std::vector<Triple> result;
    const size_t n = input.size();
    if (n == 0) {
        return result;
    }

    const size_t search_size = window.search_size;
    const size_t max_match = window.look_size;
    const uint32_t min_match = static_cast<uint32_t>(utils::effectiveMinMatchLen(
        window.min_match_len, offset_bits, length_bits));

    size_t cursor = 0;
    while (cursor < n) {
        if (core_new::is_streaming_cancel_requested()) {
            throw std::runtime_error("cancelled");
        }
        const size_t search_len = std::min(cursor, search_size);
        const size_t search_start = cursor - search_len;
        const size_t look_len = std::min(n - cursor, max_match);

        RelativeByteSlice<const std::vector<uint8_t>> search_view{input, search_start};
        RelativeByteSlice<const std::vector<uint8_t>> look_view{input, cursor};

        std::vector<Triple> step = matchAtPosition(
            engine,
            search_view,
            static_cast<uint32_t>(search_len),
            look_view,
            static_cast<uint32_t>(look_len),
            1,
            min_match);

        if (step.empty()) {
            result.emplace_back(0, 1, input[cursor]);
            ++cursor;
            continue;
        }

        const Triple& t = step[0];
        result.push_back(t);
        cursor += (t.offset == 0) ? 1 : static_cast<size_t>(t.length);
    }
    return result;
}

}  // namespace compressor::algorithm::LZMatcher

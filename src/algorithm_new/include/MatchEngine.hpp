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

/* ── 旧版备份：贪心 LZ77 走 matchAtPosition(dp_top=1) → hashChainSearch(chain_len=dp_top*8=8) ──
inline std::vector<Triple> greedyWholeInput_V1(
    const std::vector<uint8_t>& input,
    const config::Lz77WindowConfig& window,
    uint8_t offset_bits,
    uint8_t length_bits,
    models::MatchEngine engine = models::MatchEngine::HashChain) {
    std::vector<Triple> result;
    const size_t n = input.size();
    if (n == 0) return result;
    const size_t search_size = window.search_size;
    const size_t max_match = window.look_size;
    const uint32_t min_match = static_cast<uint32_t>(utils::effectiveMinMatchLen(
        window.min_match_len, offset_bits, length_bits));
    size_t cursor = 0;
    while (cursor < n) {
        if (core_new::is_streaming_cancel_requested())
            throw std::runtime_error("cancelled");
        const size_t search_len = std::min(cursor, search_size);
        const size_t search_start = cursor - search_len;
        const size_t look_len = std::min(n - cursor, max_match);
        RelativeByteSlice<const std::vector<uint8_t>> search_view{input, search_start};
        RelativeByteSlice<const std::vector<uint8_t>> look_view{input, cursor};
        std::vector<Triple> step = matchAtPosition(
            engine, search_view, static_cast<uint32_t>(search_len),
            look_view, static_cast<uint32_t>(look_len), 1, min_match);
        if (step.empty()) { result.emplace_back(0, 1, input[cursor]); ++cursor; continue; }
        const Triple& t = step[0]; result.push_back(t);
        cursor += (t.offset == 0) ? 1 : static_cast<size_t>(t.length);
    }
    return result;
}
── 旧版备份结束 ──*/

/// 贪心哈希链单步：每步查 max_chain_length 个历史位置取最长匹配，供 Deflate 贪心 LZ77 使用。
/// 与 DP 砖块 ``hashChainSearch`` 不同：贪心需要深搜哈希链，不需要控制候选数量。
inline Triple greedyHashChainStep(
    const std::vector<uint8_t>& input,
    size_t cursor,
    size_t search_size,
    size_t max_match,
    uint32_t min_match,
    std::vector<size_t>& head,
    std::vector<size_t>& prev,
    size_t hash_mask,
    size_t max_chain_length) {
    const size_t n = input.size();
    size_t best_off = 0;
    size_t best_len = 0;

    if (cursor + 2 < n) {
        size_t h = ((static_cast<size_t>(input[cursor]) << 10) ^
                    (static_cast<size_t>(input[cursor + 1]) << 5) ^
                    static_cast<size_t>(input[cursor + 2])) & hash_mask;
        size_t match_pos = head[h];
        size_t chain_len = max_chain_length;
        const size_t null = static_cast<size_t>(-1);
        while (match_pos != null && chain_len-- > 0) {
            size_t dist = cursor - match_pos;
            if (dist > search_size || dist == 0) break;
            size_t cur_len = 0;
            while (cur_len < max_match &&
                   cursor + cur_len < n &&
                   input[match_pos + cur_len] == input[cursor + cur_len]) {
                ++cur_len;
            }
            if (cur_len > best_len) {
                best_len = cur_len;
                best_off = dist;
                if (best_len >= max_match) break;
            }
            match_pos = prev[match_pos];
        }
        prev[cursor] = head[h];
        head[h] = cursor;
    }

    if (best_len >= min_match) {
        return Triple(static_cast<uint32_t>(best_off),
                      static_cast<uint32_t>(best_len), 0);
    }
    return Triple(0, 1, input[cursor]);
}

/// 整段输入上的贪心 LZ77：
///   - KMP 引擎走 kmpSearch(dp_top=1) 保持原逻辑
///   - HashChain 引擎走 greedyHashChainStep（chain_len=window.max_chain_length），
///     每次匹配区间内的所有位置也注册到哈希链保证一致性
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

    if (engine == models::MatchEngine::KMP) {
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
            std::vector<Triple> step = kmpSearch(
                search_view, static_cast<uint32_t>(search_len),
                look_view, static_cast<uint32_t>(look_len),
                1, min_match);
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

    // HashChain 贪心路径：专有实现，chain_len=window.max_chain_length
    const size_t search_size = window.search_size;
    const size_t max_match = window.look_size;
    const uint32_t min_match = static_cast<uint32_t>(utils::effectiveMinMatchLen(
        window.min_match_len, offset_bits, length_bits));

    const size_t hash_size = std::max(search_size, size_t{256});
    const size_t null = static_cast<size_t>(-1);
    std::vector<size_t> head(hash_size, null);
    std::vector<size_t> prev(n, null);

    size_t cursor = 0;
    while (cursor < n) {
        if (core_new::is_streaming_cancel_requested()) {
            throw std::runtime_error("cancelled");
        }
        Triple t = greedyHashChainStep(input, cursor, search_size, max_match,
                                       min_match, head, prev, hash_size - 1,
                                       window.max_chain_length);
        result.push_back(t);

        if (t.offset != 0 && t.length > 1) {
            for (size_t i = 1; i < t.length && cursor + i + 2 < n; ++i) {
                size_t h = ((static_cast<size_t>(input[cursor + i]) << 10) ^
                            (static_cast<size_t>(input[cursor + i + 1]) << 5) ^
                            static_cast<size_t>(input[cursor + i + 2])) & (hash_size - 1);
                prev[cursor + i] = head[h];
                head[h] = cursor + i;
            }
            cursor += static_cast<size_t>(t.length);
        } else {
            ++cursor;
        }
    }
    return result;
}

}  // namespace compressor::algorithm::LZMatcher

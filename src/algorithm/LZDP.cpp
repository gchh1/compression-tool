#include "LZDP.hpp"

#include "LZDPStreamDebug.hpp"
#include "TopMatch.hpp"

#include "DebugLog.hpp"

#include <algorithm>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <stdexcept>
#include <vector>

namespace compressor {
namespace algorithm {

size_t LZDP::cal_cost(size_t literal_count, size_t match_count) const {
    if (use_flag_encoding_) {
        return literal_count * (offset_bits_ + 1) +
               match_count * (length_bits_ + 1);
    }
    size_t tmp = literal_count;
    const size_t maxlen = (size_t{1} << length_bits_) - 1;
    size_t cost = 0;
    while (tmp > maxlen) {
        cost += offset_bits_ + length_bits_ + maxlen * 8;
        tmp -= maxlen;
    }
    cost += offset_bits_ + length_bits_ + tmp * 8;
    return cost;
}

struct ROLListNode {
    size_t literal_count{0};
    size_t match_count{0};
    size_t offset{SIZE_MAX};
    LZDP::Triple data;
};

LZDP::DpCoreResult LZDP::dp_core(
    const std::vector<uint8_t>& input, size_t search_size,
    size_t lookahead_size, size_t range,
    size_t core_begin, size_t core_end) {
    if (search_size > max_search_size_) {
        search_size = max_search_size_;
    }
    if (lookahead_size > max_look_size_) {
        lookahead_size = max_look_size_;
    }

    size_t in_len = input.size();
    if (in_len == 0 || lookahead_size == 0) {
        return DpCoreResult{};
    }

    if (core_end == SIZE_MAX || core_end > in_len) {
        core_end = in_len;
    }
    if (core_begin > core_end) {
        core_begin = core_end;
    }
    if (core_begin == core_end) {
        return DpCoreResult{};
    }

    using Node = ROLListNode;
    std::vector<Node*> dp(in_len + 1, nullptr);
    dp[core_begin] = new Node{0, 0, SIZE_MAX, Triple(0, 0, 0)};

    std::vector<size_t> head;
    std::vector<size_t> prev;
    if (match_engine_ == 1) {
        head.assign(max_search_size_ + 1, SIZE_MAX);
        prev.assign(in_len, SIZE_MAX);
        for (size_t pos = 0; pos < in_len; pos++) {
            if (pos + 2 < in_len) {
                uint16_t hash_val = ((input[pos] << 10) ^ (input[pos + 1] << 5) ^ input[pos + 2]) & (max_search_size_);
                prev[pos] = head[hash_val];
                head[hash_val] = pos;
            }
        }
    }

    for (size_t pos = core_begin; pos < core_end; pos++) {
        if (dp[pos] == nullptr) { continue; }

        const size_t lit_lit = dp[pos]->literal_count + 1;
        const size_t lit_mat = dp[pos]->match_count;
        const size_t lit_cost_new = cal_cost(lit_lit, lit_mat);

        if (dp[pos + 1] == nullptr) {
            dp[pos + 1] = new Node{lit_lit, lit_mat, pos, Triple(0, 0, input[pos])};
        } else {
            const size_t next_cost =
                cal_cost(dp[pos + 1]->literal_count, dp[pos + 1]->match_count);
            if (lit_cost_new < next_cost) {
                dp[pos + 1]->literal_count = lit_lit;
                dp[pos + 1]->match_count = lit_mat;
                dp[pos + 1]->offset = pos;
                dp[pos + 1]->data = Triple(0, 0, input[pos]);
            }
        }

        const size_t search_len =
            (pos > search_size) ? static_cast<size_t>(search_size) : pos;
        const size_t search_start = pos - search_len;
        const size_t remain = in_len - pos;
        const size_t look_len =
            (remain > lookahead_size) ? static_cast<size_t>(lookahead_size)
                                      : remain;
        if (look_len < get_min_match()) { continue; }

        struct MatchRes { size_t offset; size_t length; };
        std::vector<MatchRes> match_results;

        if (match_engine_ == 0) {
            auto kmp_results = kmpSearch(
                input.begin() + search_start, search_len,
                input.begin() + pos, look_len, range, get_min_match());
            for (auto& kr : kmp_results) {
                match_results.push_back(MatchRes{kr.offset, kr.length});
            }
        } else {
            TopMatch top_matches(range);
            size_t match_pos = prev[pos];
            size_t chain_length = range * 8;
            while (match_pos != SIZE_MAX && chain_length-- > 0) {
                size_t dist = pos - match_pos;
                if (dist > search_size || dist == 0) {
                    break;
                }

                size_t match_len = 0;
                while (match_len < look_len &&
                       input[pos + match_len] == input[match_pos + match_len]) {
                    match_len++;
                }

                if (match_len >= get_min_match()) {
                    top_matches.insert(static_cast<uint16_t>(dist),
                                       static_cast<uint16_t>(match_len));
                }
                match_pos = prev[match_pos];
            }
            for (const auto& e : top_matches.entries()) {
                match_results.push_back(MatchRes{e.offset, e.length});
            }
        }

        for (auto& kr : match_results) {
            if (kr.offset == 0 || kr.length < get_min_match()) { continue; }
            size_t target = pos + kr.length;
            if (target > in_len) { continue; }

            const size_t mat_lit = dp[pos]->literal_count;
            const size_t mat_mat = dp[pos]->match_count + 1;
            const size_t mat_cost_new = cal_cost(mat_lit, mat_mat);

            if (dp[target] == nullptr) {
                dp[target] = new Node{mat_lit, mat_mat, pos,
                                      Triple(kr.offset, kr.length, 0)};
            } else {
                const size_t tgt_cost =
                    cal_cost(dp[target]->literal_count, dp[target]->match_count);
                if (mat_cost_new < tgt_cost) {
                    dp[target]->literal_count = mat_lit;
                    dp[target]->match_count = mat_mat;
                    dp[target]->offset = pos;
                    dp[target]->data = Triple(kr.offset, kr.length, 0);
                }
            }
        }
    }

    DpCoreResult result;
    Node* cur = dp[core_end];
    size_t safety = 0;
    while (cur != nullptr && safety < in_len + 2) {
        result.triples.push_back(cur->data);
        cur = (cur->offset != SIZE_MAX) ? dp[cur->offset] : nullptr;
        safety++;
    }
    std::reverse(result.triples.begin(), result.triples.end());
    if (!result.triples.empty() && result.triples[0].offset == 0 && result.triples[0].length == 0 && result.triples[0].literal == 0) {
        result.triples.erase(result.triples.begin());
    }

    std::set<Node*> deleted;
    for (size_t i = 0; i <= in_len; i++) {
        if (dp[i] != nullptr && deleted.find(dp[i]) == deleted.end()) {
            deleted.insert(dp[i]);
            delete dp[i];
        }
    }
    return result;
}

LZDP::DpCoreResult LZDP::dp_core(
    const VirtualBuffer<3>& input, size_t search_size,
    size_t lookahead_size, size_t range,
    size_t core_begin, size_t core_end) {
    // VirtualBuffer 重载：拷贝到连续缓冲区后委托 vector 重载
    std::vector<uint8_t> flat(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        flat[i] = input[i];
    }
    return dp_core(flat, search_size, lookahead_size, range, core_begin, core_end);
}

std::vector<uint8_t> LZDP::encode_triples(
    const std::vector<Triple>& triples,
    size_t offset_bits,
    size_t length_bits,
    bool use_flag_encoding) const {
    size_t max_bits = 0;
    for (const auto& t : triples) {
        if (use_flag_encoding) {
            max_bits += (t.offset == 0) ? 9 : (1 + offset_bits + length_bits);
        } else {
            max_bits += (t.offset == 0) ? (offset_bits + length_bits + 8) : (offset_bits + length_bits);
        }
    }
    size_t max_bytes = (max_bits + 7) / 8 + 2;

    std::vector<uint8_t> encoded(max_bytes, 0);
    encoded[0] = static_cast<uint8_t>((offset_bits & 0x7F) | (use_flag_encoding ? 0x80 : 0));
    encoded[1] = static_cast<uint8_t>(length_bits);

    utils::BitWriter bw(std::span<uint8_t>(encoded.data() + 2, encoded.size() - 2));
    for (size_t i = 0; i < triples.size(); ) {
        Triple t = triples[i];
        if (use_flag_encoding) {
            if (t.offset == 0) {
                bw.writeBits(1, 1);
                bw.writeBits(t.literal, 8);
                i++;
            } else {
                bw.writeBits(0, 1);
                bw.writeBits(t.offset, static_cast<uint8_t>(offset_bits));
                bw.writeBits(t.length, static_cast<uint8_t>(length_bits));
                i++;
            }
        } else {
            if (t.offset == 0) {
                size_t run_len = 0;
                size_t run_start = i;
                while (i < triples.size() && triples[i].offset == 0) {
                    run_len++;
                    i++;
                }
                size_t max_run = (size_t{1} << length_bits) - 1;
                size_t current = 0;
                while (run_len > 0) {
                    size_t chunk = (run_len > max_run) ? max_run : run_len;
                    bw.writeBits(0, static_cast<uint8_t>(offset_bits));
                    bw.writeBits(chunk, static_cast<uint8_t>(length_bits));
                    for (size_t j = 0; j < chunk; j++) {
                        bw.writeBits(triples[run_start + current + j].literal, 8);
                    }
                    current += chunk;
                    run_len -= chunk;
                }
            } else {
                bw.writeBits(t.offset, static_cast<uint8_t>(offset_bits));
                bw.writeBits(t.length, static_cast<uint8_t>(length_bits));
                i++;
            }
        }
    }
    size_t bytes_written = bw.flush() + 2;
    encoded.resize(bytes_written);

    if (encoded.size() <= 2) { return std::vector<uint8_t>{}; }
    return encoded;
}

std::vector<uint8_t> LZDP::decompress(const std::vector<uint8_t>& input) {
    if (input.size() < 2) {
        return std::vector<uint8_t>{};
    }

    size_t ob = input[0] & 0x7F;
    bool use_flag = (input[0] & 0x80) != 0;
    size_t lb = input[1];
    if (ob < 1 || ob > 24 || lb < 1 || lb > 24) {
        throw std::runtime_error("LZDP decompress: invalid bit widths in header");
    }

    utils::BitReader br(std::span<const uint8_t>(input.data() + 2, input.size() - 2));
    std::vector<uint8_t> out;
    out.reserve(input.size() * 2);

    while (br.getRemainingBits() > 0) {
        if (use_flag) {
            if (br.getRemainingBits() == 0) break;
            if (!br.ensureBits(1)) break;
            uint64_t is_lit = br.readBits(1);
            if (is_lit) {
                if (!br.ensureBits(8)) break;
                uint64_t lit = br.readBits(8);
                out.push_back(static_cast<uint8_t>(lit));
            } else {
                if (!br.ensureBits(static_cast<uint8_t>(ob + lb))) break;
                uint64_t offset = br.readBits(static_cast<uint8_t>(ob));
                uint64_t length = br.readBits(static_cast<uint8_t>(lb));
                size_t out_size = out.size();
                if (out_size < static_cast<size_t>(offset)) {
                    throw std::runtime_error(
                        "Offset in LZDP decompression out of range: offset=" + std::to_string(offset) + " out_size=" + std::to_string(out_size));
                }
                size_t copy_start = out_size - static_cast<size_t>(offset);
                for (size_t k = 0; k < static_cast<size_t>(length); k++) {
                    out.push_back(out[copy_start + k]);
                }
            }
        } else {
            if (br.getRemainingBits() < ob + lb) break;
            if (!br.ensureBits(static_cast<uint8_t>(ob + lb))) break;
            uint64_t offset = br.readBits(static_cast<uint8_t>(ob));
            uint64_t length = br.readBits(static_cast<uint8_t>(lb));
            if (offset == 0) {
                for (size_t k = 0; k < static_cast<size_t>(length); k++) {
                    if (!br.ensureBits(8)) break;
                    uint64_t lit = br.readBits(8);
                    out.push_back(static_cast<uint8_t>(lit));
                }
            } else {
                size_t out_size = out.size();
                if (out_size < static_cast<size_t>(offset)) {
                    throw std::runtime_error(
                        "Offset in LZDP decompression out of range: offset=" + std::to_string(offset) + " out_size=" + std::to_string(out_size));
                }
                size_t copy_start = out_size - static_cast<size_t>(offset);
                for (size_t k = 0; k < static_cast<size_t>(length); k++) {
                    out.push_back(out[copy_start + k]);
                }
            }
        }
    }

    return out;
}

LZDP::DPVisualization LZDP::get_dp_visualization(
    const std::vector<uint8_t>& input, size_t search_size,
    size_t lookahead_size, size_t range) {
    if (search_size > max_search_size_) {
        search_size = max_search_size_;
    }
    if (lookahead_size > max_look_size_) {
        lookahead_size = max_look_size_;
    }

    DPVisualization viz;
    viz.input_length = input.size();
    viz.search_size = search_size;
    viz.lookahead_size = lookahead_size;

    size_t in_len = input.size();
    if (in_len == 0 || lookahead_size == 0) {
        return viz;
    }

    size_t min_match = get_min_match();

    using Node = ROLListNode;
    std::vector<Node*> dp(in_len + 1, nullptr);
    dp[0] = new Node{0, 0, SIZE_MAX, Triple(0, 0, 0)};

    std::vector<size_t> head;
    std::vector<size_t> prev;
    if (match_engine_ == 1) {
        head.assign(max_search_size_ + 1, SIZE_MAX);
        prev.assign(in_len, SIZE_MAX);
    }

    for (size_t pos = 0; pos < in_len; pos++) {
        if (match_engine_ == 1 && pos + 2 < in_len) {
            uint16_t hash_val = ((input[pos] << 10) ^ (input[pos + 1] << 5) ^ input[pos + 2]) & (max_search_size_);
            prev[pos] = head[hash_val];
            head[hash_val] = pos;
        }

        if (dp[pos] == nullptr) continue;

        if (pos + 1 <= in_len) {
            if (dp[pos + 1] == nullptr) {
                dp[pos + 1] = new Node{
                    dp[pos]->literal_count + 1, dp[pos]->match_count,
                    pos, Triple(0, 0, input[pos])};
            } else {
                size_t cur_cost = dp[pos]->literal_count + dp[pos]->match_count + 1;
                size_t next_cost = dp[pos + 1]->literal_count + dp[pos + 1]->match_count;
                if (cur_cost < next_cost) {
                    dp[pos + 1]->literal_count = dp[pos]->literal_count + 1;
                    dp[pos + 1]->match_count = dp[pos]->match_count;
                    dp[pos + 1]->offset = pos;
                    dp[pos + 1]->data = Triple(0, 0, input[pos]);
                }
            }
        }

        const size_t search_len =
            (pos > search_size) ? static_cast<size_t>(search_size) : pos;
        const size_t search_start = pos - search_len;
        const size_t remain = in_len - pos;
        const size_t look_len =
            (remain > lookahead_size) ? static_cast<size_t>(lookahead_size)
                                      : remain;
        if (look_len < get_min_match()) { continue; }

        struct MatchRes { size_t offset; size_t length; };
        std::vector<MatchRes> match_results;

        if (match_engine_ == 0) {
            auto kmp_results = kmpSearch(
                input.begin() + search_start, search_len,
                input.begin() + pos, look_len, range, get_min_match());
            for (auto& kr : kmp_results) {
                match_results.push_back({kr.offset, kr.length});
            }
        } else {
            TopMatch top_matches(range);
            size_t match_pos = prev[pos];
            size_t chain_length = range * 8;
            while (match_pos != SIZE_MAX && chain_length-- > 0) {
                size_t dist = pos - match_pos;
                if (dist > search_size || dist == 0) {
                    break;
                }

                size_t match_len = 0;
                while (match_len < look_len &&
                       input[pos + match_len] == input[match_pos + match_len]) {
                    match_len++;
                }

                if (match_len >= get_min_match()) {
                    top_matches.insert(static_cast<uint16_t>(dist),
                                       static_cast<uint16_t>(match_len));
                }
                match_pos = prev[match_pos];
            }
            for (const auto& e : top_matches.entries()) {
                match_results.push_back({e.offset, e.length});
            }
        }

        DPStep step;
        step.position = pos;
        step.best_token_count = dp[pos]->literal_count + dp[pos]->match_count;

        for (auto& kr : match_results) {
            if (kr.offset == 0) continue;
            size_t target = pos + kr.length;
            if (target > in_len) continue;

            size_t new_count = dp[pos]->literal_count + dp[pos]->match_count + 1;

            bool is_chosen = false;
            if (dp[target] == nullptr) {
                dp[target] = new Node{
                    dp[pos]->literal_count, dp[pos]->match_count + 1,
                    pos, Triple(kr.offset, kr.length, 0)};
                is_chosen = true;
            } else {
                size_t tgt_cost = dp[target]->literal_count + dp[target]->match_count;
                if (new_count < tgt_cost) {
                    dp[target]->literal_count = dp[pos]->literal_count;
                    dp[target]->match_count = dp[pos]->match_count + 1;
                    dp[target]->offset = pos;
                    dp[target]->data = Triple(kr.offset, kr.length, 0);
                    is_chosen = true;
                }
            }

            step.candidates.emplace_back(
                Triple{kr.offset, kr.length, 0}, is_chosen);
        }

        if (!step.candidates.empty()) {
            viz.steps.push_back(std::move(step));
        }
    }

    if (dp[in_len] != nullptr) {
        viz.optimal_path.clear();
        Node* cur = dp[in_len];
        while (cur != nullptr) {
            viz.optimal_path.push_back(cur->data);
            cur = (cur->offset != SIZE_MAX) ? dp[cur->offset] : nullptr;
        }
        std::reverse(viz.optimal_path.begin(), viz.optimal_path.end());
    }

    {
        viz.dp_array.reserve(in_len + 1);
        for (size_t i = 0; i <= in_len; i++) {
            DPState st;
            st.position = i;
            if (dp[i] != nullptr) {
                st.reachable = true;
                st.token_count = dp[i]->literal_count + dp[i]->match_count;
                st.literal_count = dp[i]->literal_count;
                st.match_count = dp[i]->match_count;
                st.choice = dp[i]->data;
                st.predecessor = dp[i]->offset;
            } else {
                st.reachable = false;
                st.token_count = 0;
                st.literal_count = 0;
                st.match_count = 0;
                st.predecessor = SIZE_MAX;
            }
            viz.dp_array.push_back(st);
        }
    }

    std::set<Node*> deleted;
    for (size_t i = 0; i <= in_len; i++) {
        if (dp[i] != nullptr && deleted.find(dp[i]) == deleted.end()) {
            deleted.insert(dp[i]);
            delete dp[i];
        }
    }

    return viz;
}

uint32_t LZDP_Streaming::cal_path_cost(uint32_t literal_count, uint32_t match_count) const {
    if (use_flag_encoding_) {
        return literal_count * static_cast<uint32_t>(offset_bits_ + 1) +
               match_count * static_cast<uint32_t>(length_bits_ + 1);
    }
    uint32_t tmp = literal_count;
    const uint32_t maxlen = (uint32_t{1} << length_bits_) - 1u;
    uint32_t cost = 0;
    while (tmp > maxlen) {
        cost += static_cast<uint32_t>(offset_bits_ + length_bits_) + maxlen * 8u;
        tmp -= maxlen;
    }
    cost += static_cast<uint32_t>(offset_bits_ + length_bits_) + tmp * 8u;
    return cost;
}

void lzdp_streaming_collect_one_index(LZDP_Streaming& self, size_t pos_idx, uint32_t abs_pos) {
    auto& cur = self.streaming_dp_.cell_at(abs_pos);
    const uint32_t cur_cost = cur.cost;
    const uint16_t link_len = cur.length;
    const uint16_t link_off = cur.offset;
    const uint32_t cur_lit = cur.literal_count;
    const uint32_t cur_mat = cur.match_count;

    if (abs_pos < 20) {
        DEBUG_LOG(
            "[LZDP_Streaming] COLLECT pos=%u pos_idx=%zu cur.cost=%u lit=%u mat=%u len=%u off=%u",
            abs_pos, pos_idx, cur_cost, cur_lit, cur_mat, link_len, link_off);
    }

    const uint32_t lit_lit = cur_lit + 1u;
    const uint32_t lit_mat = cur_mat;
    const uint32_t lit_cost_new = self.cal_path_cost(lit_lit, lit_mat);
    auto& next_lit = self.streaming_dp_.cell_at(abs_pos + 1);
    if (lit_cost_new < next_lit.cost) {
        next_lit.cost = lit_cost_new;
        next_lit.literal_count = lit_lit;
        next_lit.match_count = lit_mat;
        next_lit.length = 0;
        next_lit.offset = self.input_buffer_[pos_idx];
    }

    const size_t remain_len = self.input_buffer_.size() - pos_idx;
    const size_t look_len =
        (remain_len > self.LOOKAHEAD_SIZE) ? self.LOOKAHEAD_SIZE : remain_len;

    struct MatchRes {
        size_t offset;
        size_t length;
    };
    std::vector<MatchRes> match_results;

    if (look_len >= self.MIN_MATCH) {
        if (self.match_engine_ == 0) {
            const size_t search_len =
                (abs_pos > self.SEARCH_SIZE) ? self.SEARCH_SIZE : static_cast<size_t>(abs_pos);
            const size_t search_start = pos_idx - search_len;
            const auto kmp_results = kmpSearch(
                self.input_buffer_.begin() + search_start, search_len,
                self.input_buffer_.begin() + pos_idx, look_len, self.DP_TOP, self.MIN_MATCH);
            for (const auto& kr : kmp_results) {
                match_results.push_back(MatchRes{kr.offset, kr.length});
            }
        } else if (pos_idx + 2 < self.input_buffer_.size()) {
            const size_t slot = self.hashBucket3(pos_idx);
            self.prev_buf_[pos_idx] = self.head_[slot];
            self.head_[slot] = static_cast<uint32_t>(pos_idx);

            TopMatch top_matches(self.DP_TOP);
            uint32_t match_buf_idx = self.prev_buf_[pos_idx];
            size_t chain_length = self.DP_TOP * 8;
            while (match_buf_idx != UINT32_MAX && chain_length-- > 0) {
                const size_t dist = pos_idx - match_buf_idx;
                if (dist > self.SEARCH_SIZE || dist == 0) {
                    break;
                }

                size_t match_len = 0;
                while (match_len < look_len &&
                       self.input_buffer_[pos_idx + match_len] ==
                           self.input_buffer_[match_buf_idx + match_len]) {
                    match_len++;
                }

                if (match_len >= self.MIN_MATCH) {
                    top_matches.insert(static_cast<uint16_t>(dist),
                                       static_cast<uint16_t>(match_len));
                }
                match_buf_idx = self.prev_buf_[match_buf_idx];
            }
            for (const auto& e : top_matches.entries()) {
                match_results.push_back(MatchRes{e.offset, e.length});
            }
        }
    }

    for (const auto& kr : match_results) {
        if (kr.offset == 0 || kr.length < self.MIN_MATCH) {
            continue;
        }
        const uint32_t mat_lit = cur_lit;
        const uint32_t mat_mat = cur_mat + 1u;
        const uint32_t mat_cost_new = self.cal_path_cost(mat_lit, mat_mat);
        auto& nm = self.streaming_dp_.cell_at(abs_pos + static_cast<uint32_t>(kr.length));
        if (mat_cost_new < nm.cost) {
            nm.cost = mat_cost_new;
            nm.literal_count = mat_lit;
            nm.match_count = mat_mat;
            nm.length = static_cast<uint16_t>(kr.length);
            nm.offset = static_cast<uint16_t>(kr.offset);
        }
    }

    if (self.link_lengths_.size() <= abs_pos) {
        self.link_lengths_.resize(static_cast<size_t>(abs_pos) + 1);
        self.link_offsets_.resize(static_cast<size_t>(abs_pos) + 1);
    }
    self.link_lengths_[abs_pos] = link_len;
    self.link_offsets_[abs_pos] = link_off;
    self.spill_a_.writePackedLink(self.spill_spec_, link_len, link_off);

    if (abs_pos == 0) {
        LZDP_STREAM_LOG("COLLECT", "index=0 cost=%u len=%u off=%u", cur_cost, link_len, link_off);
    }
}

LZDP_Streaming::LZDP_Streaming(size_t search_size, size_t lookahead_size,
                               size_t min_match, size_t dp_top,
                               bool use_flag_encoding, int match_engine)
    : SEARCH_SIZE(search_size),
      LOOKAHEAD_SIZE(lookahead_size),
      MIN_MATCH(min_match),
      DP_TOP(dp_top),
      use_flag_encoding_(use_flag_encoding),
      match_engine_(match_engine) {
    if (SEARCH_SIZE < 16) SEARCH_SIZE = 16;
    if (LOOKAHEAD_SIZE < 4) LOOKAHEAD_SIZE = 4;
    // Must match ``utils::calcBitWidth``: distances and match
    // lengths can equal SEARCH_SIZE / LOOKAHEAD_SIZE inclusive (e.g. 256 needs 9 bits).
    const size_t sz_ob = std::max<size_t>(SEARCH_SIZE, size_t{1});
    const size_t sz_lb = std::max<size_t>(LOOKAHEAD_SIZE, size_t{1});
    offset_bits_ = static_cast<size_t>(std::bit_width(sz_ob));
    length_bits_ = static_cast<size_t>(std::bit_width(sz_lb));
    if (MIN_MATCH == 0) {
        MIN_MATCH = get_match_bits() / 8 + 1;
    }
    reset();
}

auto LZDP_Streaming::reset(void) -> void {
    input_buffer_.clear();

    streaming_dp_.reset(LOOKAHEAD_SIZE);

    head_.assign(std::max(SEARCH_SIZE, size_t{1}), UINT32_MAX);
    prev_buf_.clear();
    
    window_abs_pos_ = 0;
    current_i_ = 0;
    total_in_len_ = 0;
    
    spill_spec_ = PackedDpLinkSpec::fromBitWidths(static_cast<uint8_t>(offset_bits_),
                                                  static_cast<uint8_t>(length_bits_));
    spill_a_.rebind(&temp_file_A_);
    spill_b_.rebind(&temp_file_B_);

    total_tokens_ = 0;
    emitted_tokens_ = 0;
    link_lengths_.clear();
    link_offsets_.clear();
    token_lengths_.clear();
    token_offsets_.clear();
    emit_lzdp_raw_header_done_ = false;
    writer_.resetPendingBits();

    state_ = State::COLLECT_INPUT;
}

auto LZDP_Streaming::hashBucket3(size_t pos_idx) const -> size_t {
    const uint32_t h = (uint32_t(input_buffer_[pos_idx] << 10) ^
                        uint32_t(input_buffer_[pos_idx + 1] << 5) ^
                        uint32_t(input_buffer_[pos_idx + 2]));
    if (head_.empty()) {
        return 0;
    }
    if (SEARCH_SIZE > 1 && (SEARCH_SIZE & (SEARCH_SIZE - 1)) == 0) {
        return size_t(h) & (SEARCH_SIZE - 1);
    }
    return size_t(h % head_.size());
}

auto LZDP_Streaming::reseedHashChainPrefix(size_t end_exclusive) -> void {
    std::fill(head_.begin(), head_.end(), UINT32_MAX);
    const size_t n = std::min(end_exclusive, input_buffer_.size());
    for (size_t i = 0; i + 2 < n; ++i) {
        if ((i & size_t{2047}) == 0 && algorithm::g_cancel_callback &&
            algorithm::g_cancel_callback()) {
            throw std::runtime_error("cancelled");
        }
        const size_t slot = hashBucket3(i);
        prev_buf_[i] = head_[slot];
        head_[slot] = static_cast<uint32_t>(i);
    }
}

auto LZDP_Streaming::handleCollectInput(AlgorithmStatus& status, bool is_last_chunk) -> void {
    size_t remain = reader_.getRemainSize();
    if (remain > 0) {
        size_t old_len = input_buffer_.size();
        input_buffer_.resize(old_len + remain);
        size_t copied = reader_.readBytes(input_buffer_.data() + old_len, remain);
        if (copied < remain) {
            input_buffer_.resize(old_len + copied);
        }
    }

    if (prev_buf_.size() != input_buffer_.size()) {
        prev_buf_.resize(input_buffer_.size(), UINT32_MAX);
    }

    size_t processable = 0;
    if (is_last_chunk) {
        processable = input_buffer_.size() - current_i_;
    } else {
        if (input_buffer_.size() > LOOKAHEAD_SIZE) {
            size_t available_for_pos = input_buffer_.size() - LOOKAHEAD_SIZE;
            if (available_for_pos > current_i_) {
                processable = available_for_pos - current_i_;
            }
        }
    }
    
    if (processable > 0) {
        for (size_t k = 0; k < processable; ++k) {
            if ((k & size_t{4095}) == 0 && algorithm::g_cancel_callback &&
                algorithm::g_cancel_callback()) {
                throw std::runtime_error("cancelled");
            }

            size_t pos_idx = current_i_ + k;
            uint32_t abs_pos = window_abs_pos_ + static_cast<uint32_t>(pos_idx);

            lzdp_streaming_collect_one_index(*this, pos_idx, abs_pos);
        }
        current_i_ += processable;
        const uint32_t commit_until =
            window_abs_pos_ + static_cast<uint32_t>(current_i_);
        streaming_dp_.rotate(commit_until);
        LZDP_STREAM_LOG("COLLECT_SLIDE", "commit_until=%u current_i=%zu window_abs=%u buf=%zu",
                        commit_until, current_i_, window_abs_pos_, input_buffer_.size());
        if (algorithm::g_cancel_callback && algorithm::g_cancel_callback()) {
            throw std::runtime_error("cancelled");
        }
    }

    if (!is_last_chunk) {
        size_t next_abs_pos = window_abs_pos_ + current_i_;
        size_t keep_start_abs = (next_abs_pos > SEARCH_SIZE) ? (next_abs_pos - SEARCH_SIZE) : 0;
        size_t keep_start_idx = keep_start_abs - window_abs_pos_;

        if (keep_start_idx > 0) {
            streaming_dp_.prune_before(static_cast<uint32_t>(keep_start_abs));
            input_buffer_.erase(input_buffer_.begin(), input_buffer_.begin() + keep_start_idx);
            prev_buf_.erase(prev_buf_.begin(), prev_buf_.begin() + keep_start_idx);
            window_abs_pos_ = static_cast<uint32_t>(keep_start_abs);
            current_i_ -= keep_start_idx;
            if (match_engine_ == 1) {
                reseedHashChainPrefix(current_i_);
            }
        }
        status.need_input = true;
    } else {
        total_in_len_ = window_abs_pos_ + static_cast<uint32_t>(current_i_);

        StreamingDpCell& end_cell = streaming_dp_.cell_at(total_in_len_);
        if (link_lengths_.size() <= total_in_len_) {
            link_lengths_.resize(static_cast<size_t>(total_in_len_) + 1);
            link_offsets_.resize(static_cast<size_t>(total_in_len_) + 1);
        }
        link_lengths_[total_in_len_] = end_cell.length;
        link_offsets_[total_in_len_] = end_cell.offset;
        spill_a_.writePackedLink(spill_spec_, end_cell.length, end_cell.offset);
        spill_a_.flush();

        DEBUG_LOG("[LZDP_Streaming] COLLECT done: total_in_len=%u final_state=(len=%u off=%u cost=%u)",
                  total_in_len_, end_cell.length, end_cell.offset, end_cell.cost);

        state_ = State::BACKTRACK;
    }
}

auto LZDP_Streaming::handleBacktrack(AlgorithmStatus& status, bool is_last_chunk) -> void {
    if (algorithm::g_cancel_callback && algorithm::g_cancel_callback()) {
        throw std::runtime_error("cancelled");
    }

    uint32_t cur = total_in_len_;
    size_t bt_count = 0;
    token_lengths_.clear();
    token_offsets_.clear();

    while (cur > 0) {
        if ((cur & 0xFFFF) == 0 && algorithm::g_cancel_callback && algorithm::g_cancel_callback()) {
            throw std::runtime_error("cancelled");
        }

        uint16_t length = 0;
        uint16_t offset = 0;
        if (cur < link_lengths_.size()) {
            length = link_lengths_[cur];
            offset = link_offsets_[cur];
        } else {
            PackedDpLinkBackwardWindow readerA(temp_file_A_, spill_spec_);
            readerA.readPair(cur, length, offset);
        }

        if (bt_count < 10) {
            DEBUG_LOG("[LZDP_Streaming] BACKTRACK cur=%u len=%u off=%u", cur, length, offset);
        }
        bt_count++;

        spill_b_.writePackedLink(spill_spec_, length, offset);
        token_lengths_.push_back(length);
        token_offsets_.push_back(offset);
        total_tokens_++;

        if (length == 0) {
            cur -= 1;
        } else {
            cur -= length;
        }
    }

    spill_b_.flush();

    LZDP_STREAM_LOG("BACKTRACK", "done total_tokens=%zu total_in_len=%u", total_tokens_,
                    total_in_len_);

    state_ = State::EMIT_TOKENS;
}

auto LZDP_Streaming::handleEmitTokens(AlgorithmStatus& status, bool is_last_chunk) -> void {
    if (token_lengths_.size() != total_tokens_ || token_offsets_.size() != total_tokens_) {
        status.done = true;
        return;
    }

    if (!emit_lzdp_raw_header_done_) {
        // Two-byte header must be byte-aligned at the start of the LZDP bitstream. Pending bits
        // in ``BitWriter`` (e.g. stale state or carry from a bug) would splice header into the
        // wrong bit positions and break ``readBytes`` on decompress (invalid bit widths).
        writer_.resetPendingBits();
        if (!writer_.ensureSpace(16)) {
            status.need_output = true;
            return;
        }
        const uint8_t h[2] = {
            static_cast<uint8_t>((offset_bits_ & 0x7F) | (use_flag_encoding_ ? 0x80u : 0u)),
            static_cast<uint8_t>(length_bits_)};
        if (writer_.writeBytes(h, 2) != 2) {
            status.need_output = true;
            return;
        }
        DEBUG_LOG("[LZDP_Streaming] emit header: hdr0=0x%02x hdr1=0x%02x offset_bits=%zu length_bits=%zu use_flag=%d total_tokens=%zu",
                  h[0], h[1], offset_bits_, length_bits_, use_flag_encoding_, total_tokens_);
        emit_lzdp_raw_header_done_ = true;
    }

    while (emitted_tokens_ < total_tokens_) {
        if ((emitted_tokens_ & 0xFFFF) == 0 && algorithm::g_cancel_callback && algorithm::g_cancel_callback()) {
            throw std::runtime_error("cancelled");
        }

        const uint64_t idx = total_tokens_ - 1 - emitted_tokens_;
        const uint16_t len = token_lengths_[idx];
        const uint16_t off = token_offsets_[idx];

        if (emitted_tokens_ < 10) {
            DEBUG_LOG("[LZDP_Streaming] EMIT token[%zu]: idx=%llu len=%u off=%u",
                      emitted_tokens_, (unsigned long long)idx, len, off);
        }

        // non-flag: batch literal runs like ``encode_triples`` (not one (0,1)+byte per token).
        if (!use_flag_encoding_ && len == 0) {
            size_t run_len = 0;
            for (int64_t scan = static_cast<int64_t>(idx); scan >= 0 &&
                 token_lengths_[static_cast<size_t>(scan)] == 0;
                 --scan) {
                run_len++;
            }
            const size_t max_run = (size_t{1} << length_bits_) - 1;
            size_t pos = 0;
            while (pos < run_len) {
                if (!writer_.ensureSpace(64)) {
                    status.need_output = true;
                    return;
                }
                const size_t chunk = std::min(run_len - pos, max_run);
                writer_.writeBits(0, static_cast<uint8_t>(offset_bits_));
                writer_.writeBits(chunk, static_cast<uint8_t>(length_bits_));
                for (size_t j = 0; j < chunk; j++) {
                    const uint64_t tok_idx = idx - pos - j;
                    writer_.writeBits(token_offsets_[tok_idx], 8);
                }
                pos += chunk;
            }
            emitted_tokens_ += run_len;
            continue;
        }

        if (!writer_.ensureSpace(64)) {
            status.need_output = true;
            return;
        }

        if (use_flag_encoding_) {
            if (len == 0) {
                writer_.writeBits(1, 1);
                writer_.writeBits(off, 8);
            } else {
                writer_.writeBits(0, 1);
                writer_.writeBits(off, static_cast<uint8_t>(offset_bits_));
                writer_.writeBits(len, static_cast<uint8_t>(length_bits_));
            }
        } else {
            writer_.writeBits(off, static_cast<uint8_t>(offset_bits_));
            writer_.writeBits(len, static_cast<uint8_t>(length_bits_));
        }

        emitted_tokens_++;
    }
    
    writer_.flush();
    LZDP_STREAM_LOG("EMIT", "done writer_bytes=%zu total_tokens=%zu", writer_.getBytesWritten(),
                    total_tokens_);
    status.done = true;
}

auto LZDP_Streaming::handle(AlgorithmStatus& status, bool is_last_chunk) -> void {
    while (true) {
        (this->*kStateHandlers[static_cast<size_t>(state_)])(status, is_last_chunk);

        if (status.need_input || status.need_output || status.done) {
            return;
        }
    }
}

LZDPDecompress_Streaming::LZDPDecompress_Streaming(bool use_flag_encoding)
    : use_flag_encoding_(use_flag_encoding) {
    reset();
}

auto LZDPDecompress_Streaming::reset(void) -> void {
    read_header_ = false;
    lzdp_hdr_acc_[0] = 0;
    lzdp_hdr_acc_[1] = 0;
    lzdp_hdr_acc_len_ = 0;
    offset_bits_ = 0;
    length_bits_ = 0;
    output_buffer_.clear();
    output_flush_idx_ = 0;
    lzdp_dec_iter_ = 0;
}

auto LZDPDecompress_Streaming::handle(AlgorithmStatus& status, bool is_last_chunk) -> void {
    if (algorithm::g_cancel_callback && algorithm::g_cancel_callback()) {
        throw std::runtime_error("cancelled");
    }

    if (!read_header_) {
        // Match ``LZDP::decompress``: two raw bytes before the bit-packed token stream.
        while (lzdp_hdr_acc_len_ < 2) {
            const size_t n = reader_.readBytes(
                lzdp_hdr_acc_ + lzdp_hdr_acc_len_,
                static_cast<size_t>(2 - lzdp_hdr_acc_len_));
            if (n == 0) {
                if (is_last_chunk) {
                    status.done = true;
                } else {
                    status.need_input = true;
                }
                return;
            }
            lzdp_hdr_acc_len_ = static_cast<uint8_t>(lzdp_hdr_acc_len_ + static_cast<uint8_t>(n));
        }
        const uint8_t hdr0 = lzdp_hdr_acc_[0];
        const uint8_t hdr1 = lzdp_hdr_acc_[1];
        offset_bits_ = static_cast<size_t>(hdr0 & 0x7F);
        use_flag_encoding_ = (hdr0 & 0x80) != 0;
        length_bits_ = static_cast<size_t>(hdr1);

        DEBUG_LOG("[LZDPDecompress_Streaming] header: hdr0=0x%02x hdr1=0x%02x offset_bits=%zu length_bits=%zu use_flag=%d",
                  hdr0, hdr1, offset_bits_, length_bits_, use_flag_encoding_);

        if (offset_bits_ < 1 || offset_bits_ > 24 || length_bits_ < 1 || length_bits_ > 24) {
            throw std::runtime_error("LZDP decompress: invalid bit widths in header");
        }
        read_header_ = true;
        lzdp_hdr_acc_len_ = 0;
    }

    while (output_flush_idx_ == output_buffer_.size()) {
        if (algorithm::g_cancel_callback && algorithm::g_cancel_callback()) {
            throw std::runtime_error("cancelled");
        }

        ++lzdp_dec_iter_;
        if (lzdp_dec_iter_ > 10000000) {
            DEBUG_LOG("[LZDPDecompress_Streaming] SAFETY BREAK: iter=%zu out_buf_size=%zu",
                      lzdp_dec_iter_, output_buffer_.size());
            status.done = true;
            return;
        }
        
        if (reader_.getRemainingBits() == 0) {
            if (is_last_chunk) status.done = true;
            else status.need_input = true;
            return;
        }

        if (use_flag_encoding_) {
            if (reader_.getRemainingBits() < 1) {
                if (is_last_chunk) status.done = true;
                else status.need_input = true;
                return;
            }
            const uint64_t is_lit = reader_.readBits(1);
            if (is_lit != 0) {
                if (!reader_.ensureBits(8)) {
                    if (is_last_chunk) status.done = true;
                    else status.need_input = true;
                    return;
                }
                const uint8_t lit = static_cast<uint8_t>(reader_.readBits(8));
                if (lzdp_dec_iter_ < 20) {
                    DEBUG_LOG("[LZDPDecompress_Streaming] DECODE iter=%zu LITERAL lit=0x%02x out_size=%zu",
                              lzdp_dec_iter_, lit, output_buffer_.size());
                }
                output_buffer_.push_back(lit);
            } else {
                if (!reader_.ensureBits(static_cast<uint8_t>(offset_bits_ + length_bits_))) {
                    if (is_last_chunk) status.done = true;
                    else status.need_input = true;
                    return;
                }
                const uint32_t offset =
                    static_cast<uint32_t>(reader_.readBits(static_cast<uint8_t>(offset_bits_)));
                const uint32_t length =
                    static_cast<uint32_t>(reader_.readBits(static_cast<uint8_t>(length_bits_)));

                if (lzdp_dec_iter_ < 20) {
                    DEBUG_LOG("[LZDPDecompress_Streaming] DECODE iter=%zu MATCH off=%u len=%u out_size=%zu",
                              lzdp_dec_iter_, offset, length, output_buffer_.size());
                }

                const size_t out_size = output_buffer_.size();
                if (out_size < static_cast<size_t>(offset)) {
                    throw std::runtime_error(
                        "Offset in LZDP decompression out of range: offset=" +
                        std::to_string(offset) + " out_size=" + std::to_string(out_size));
                }
                const size_t copy_start = out_size - static_cast<size_t>(offset);
                for (size_t k = 0; k < length; k++) {
                    output_buffer_.push_back(output_buffer_[copy_start + k]);
                }
            }
        } else {
            if (reader_.getRemainingBits() < get_match_bits()) {
                if (is_last_chunk) status.done = true;
                else status.need_input = true;
                return;
            }
            if (!reader_.ensureBits(static_cast<uint8_t>(offset_bits_ + length_bits_))) {
                if (is_last_chunk) status.done = true;
                else status.need_input = true;
                return;
            }
            const uint32_t offset =
                static_cast<uint32_t>(reader_.readBits(static_cast<uint8_t>(offset_bits_)));
            const uint32_t length =
                static_cast<uint32_t>(reader_.readBits(static_cast<uint8_t>(length_bits_)));

            if (lzdp_dec_iter_ < 20) {
                DEBUG_LOG("[LZDPDecompress_Streaming] DECODE iter=%zu off=%u len=%u out_size=%zu",
                          lzdp_dec_iter_, offset, length, output_buffer_.size());
            }

            if (offset == 0) {
                for (size_t k = 0; k < length; k++) {
                    if (!reader_.ensureBits(8)) {
                        if (is_last_chunk) status.done = true;
                        else status.need_input = true;
                        return;
                    }
                    const uint8_t lit = static_cast<uint8_t>(reader_.readBits(8));
                    output_buffer_.push_back(lit);
                }
            } else {
                const size_t out_size = output_buffer_.size();
                if (out_size < static_cast<size_t>(offset)) {
                    throw std::runtime_error(
                        "Offset in LZDP decompression out of range: offset=" +
                        std::to_string(offset) + " out_size=" + std::to_string(out_size));
                }
                const size_t copy_start = out_size - static_cast<size_t>(offset);
                for (size_t k = 0; k < length; k++) {
                    output_buffer_.push_back(output_buffer_[copy_start + k]);
                }
            }
        }

        if (output_buffer_.size() - output_flush_idx_ >= 65536) {
            break;
        }
    }

    if (output_flush_idx_ < output_buffer_.size()) {
        const size_t available = output_buffer_.size() - output_flush_idx_;
        if (writer_.ensureSpace(available * 8)) {
            const size_t n = writer_.writeBytes(output_buffer_.data() + output_flush_idx_,
                                                  available);
            output_flush_idx_ += n;
            // Do not shrink ``output_buffer_`` here: match offset is backward distance from
            // the current decode tail (LZ77, same as LZSS). Dropping prefix bytes would break
            // copies unless we keep a ring buffer + decode_base (see streaming-compression-design §1.9).
        } else {
            status.need_output = true;
        }
    } else if (is_last_chunk && reader_.getRemainingBits() == 0) {
        DEBUG_LOG("[LZDPDecompress_Streaming] done: output_buffer_size=%zu output_flush_idx=%zu",
                  output_buffer_.size(), output_flush_idx_);
        status.done = true;
    }
}

} // namespace algorithm
} // namespace compressor

#include "DPFlate.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include <filesystem>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "BitWriter.hpp"
#include "HuffmanTree.hpp"
#include "KMPMatcher.hpp"

namespace compressor::algorithm {

DPFlate::DPFlate(size_t search_size, size_t lookahead_size, size_t min_match,
                 size_t dp_top, size_t dp_sub_match_max)
    : SEARCH_SIZE(search_size),
      LOOKAHEAD_SIZE(lookahead_size),
      MIN_MATCH(min_match),
      DP_TOP(dp_top),
      DP_SUB_MATCH_MAX(dp_sub_match_max) {
    reset();
}

auto DPFlate::reset(void) -> void {
    input_buffer_.clear();

    dp_slot_count_ = std::max(size_t{64}, 2 * LOOKAHEAD_SIZE + 2);
    dp_states_.assign(dp_slot_count_, DpState{});
    dp_states_[0].cost = 0;

    head_.assign(std::max(SEARCH_SIZE, size_t{1}), UINT32_MAX);
    prev_buf_.clear();

    window_abs_pos_ = 0;
    current_i_ = 0;
    total_in_len_ = 0;

    spill_spec_ = PackedDpLinkSpec::fromWindow(SEARCH_SIZE, LOOKAHEAD_SIZE);
    spill_a_.rebind(&temp_file_A_);
    spill_b_.rebind(&temp_file_B_);

    total_tokens_ = 0;
    emitted_tokens_ = 0;

    huff_reset_entropy_tables();

    auto calc_bit_width = [](size_t v) -> int {
        int bits = 0;
        if (v == 0) return 1;
        v--;
        while (v > 0) {
            bits++;
            v >>= 1;
        }
        return bits == 0 ? 1 : bits;
    };

    const int ob = calc_bit_width(SEARCH_SIZE);
    const int lb = calc_bit_width(LOOKAHEAD_SIZE);
    if (use_flag_encoding_) {
        lit_cost_ = 9;
        match_cost_ = 1 + ob + lb;
    } else {
        lit_cost_ = 8;
        match_cost_ = ob + lb;
    }

    state_ = DPFlateState::COLLECT_INPUT;
}

auto DPFlate::hashBucket3(size_t pos_idx) const -> size_t {
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

auto DPFlate::reseedHashChainPrefix(size_t end_exclusive) -> void {
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

/**
 * COLLECT_INPUT 的「DP Core」单步：对绝对位置 ``abs_pos`` 做字面量/匹配松弛，写 temp A 链节并清空环形槽。
 * 与 ``LZDP_OutOfCore`` 的 Phase1 同构；缩写见 ``docs/缩写对照表.md``。
 */
void dpflate_collect_input_one_index(DPFlate& self, size_t pos_idx, uint32_t abs_pos) {
    auto& cur = self.dp_states_[abs_pos % self.dp_slot_count_];

    auto& next_lit = self.dp_states_[(abs_pos + 1) % self.dp_slot_count_];
    if (cur.cost + self.lit_cost_ < next_lit.cost) {
        next_lit.cost = cur.cost + self.lit_cost_;
        next_lit.length = 0;
        next_lit.offset = self.input_buffer_[pos_idx];
    }

    const size_t remain_len = self.input_buffer_.size() - pos_idx;
    const size_t look_len =
        (remain_len > self.LOOKAHEAD_SIZE) ? self.LOOKAHEAD_SIZE : remain_len;

    if (look_len >= self.MIN_MATCH) {
        if (self.match_engine_ == 0) {
            const size_t search_len = (abs_pos > self.SEARCH_SIZE) ? self.SEARCH_SIZE : abs_pos;
            const size_t search_start = pos_idx - search_len;
            auto kmp_results = kmpSearch(
                self.input_buffer_.begin() + search_start, search_len,
                self.input_buffer_.begin() + pos_idx, look_len, self.DP_TOP, self.MIN_MATCH);
            for (auto& kr : kmp_results) {
                auto& nm = self.dp_states_[(abs_pos + kr.length) % self.dp_slot_count_];
                if (cur.cost + self.match_cost_ < nm.cost) {
                    nm.cost = cur.cost + self.match_cost_;
                    nm.length = static_cast<uint16_t>(kr.length);
                    nm.offset = static_cast<uint16_t>(kr.offset);
                }
            }
        } else {
            if (pos_idx + 2 < self.input_buffer_.size()) {
                const size_t slot = self.hashBucket3(pos_idx);
                self.prev_buf_[pos_idx] = self.head_[slot];
                self.head_[slot] = static_cast<uint32_t>(pos_idx);

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
                        auto& nm = self.dp_states_[(abs_pos + match_len) % self.dp_slot_count_];
                        if (cur.cost + self.match_cost_ < nm.cost) {
                            nm.cost = cur.cost + self.match_cost_;
                            nm.length = static_cast<uint16_t>(match_len);
                            nm.offset = static_cast<uint16_t>(dist);
                        }
                    }
                    match_buf_idx = self.prev_buf_[match_buf_idx];
                }
            }
        }
    }

    self.spill_a_.writePackedLink(self.spill_spec_, cur.length, cur.offset);

    cur.cost = UINT32_MAX;
    cur.length = 0;
    cur.offset = 0;
}

auto DPFlate::handleCollectInput(AlgorithmStatus& status, bool is_last_chunk) -> void {
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

            dpflate_collect_input_one_index(*this, pos_idx, abs_pos);
        }
        current_i_ += processable;
        if (algorithm::g_cancel_callback && algorithm::g_cancel_callback()) {
            throw std::runtime_error("cancelled");
        }
    }
    
    if (!is_last_chunk) {
        size_t next_abs_pos = window_abs_pos_ + current_i_;
        size_t keep_start_abs = (next_abs_pos > SEARCH_SIZE) ? (next_abs_pos - SEARCH_SIZE) : 0;
        size_t keep_start_idx = keep_start_abs - window_abs_pos_;
        
        if (keep_start_idx > 0) {
            std::vector<uint8_t> new_buf(input_buffer_.begin() + keep_start_idx, input_buffer_.end());
            std::vector<uint32_t> new_prev(prev_buf_.begin() + keep_start_idx, prev_buf_.end());
            input_buffer_ = std::move(new_buf);
            prev_buf_ = std::move(new_prev);
            window_abs_pos_ = static_cast<uint32_t>(keep_start_abs);
            current_i_ -= keep_start_idx;
            if (match_engine_ == 1) {
                reseedHashChainPrefix(current_i_);
            }
        }
        status.need_input = true;
    } else {
        total_in_len_ = window_abs_pos_ + static_cast<uint32_t>(current_i_);
        
        DpState& cur = dp_states_[total_in_len_ % dp_slot_count_];
        spill_a_.writePackedLink(spill_spec_, cur.length, cur.offset);
        spill_a_.flush();
        
        state_ = DPFlateState::BACKTRACK;
    }
}

auto DPFlate::handleBacktrack(AlgorithmStatus& status, bool is_last_chunk) -> void {
    (void)is_last_chunk;
    if (algorithm::g_cancel_callback && algorithm::g_cancel_callback()) {
        throw std::runtime_error("cancelled");
    }

    PackedDpLinkBackwardWindow readerA(temp_file_A_, spill_spec_);
    uint32_t cur = total_in_len_;
    size_t literal_run_len_3hm = 0;

    while (cur > 0) {
        if ((cur & 0xFFFF) == 0 && algorithm::g_cancel_callback && algorithm::g_cancel_callback()) {
            throw std::runtime_error("cancelled");
        }

        uint16_t length = 0;
        uint16_t offset = 0;
        readerA.readPair(cur, length, offset);

        spill_b_.writePackedLink(spill_spec_, length, offset);
        total_tokens_++;

        huff_backtrack_accumulate_token(length, offset, literal_run_len_3hm);

        if (length == 0) {
            cur -= 1;
        } else {
            cur -= length;
        }
    }

    huff_backtrack_finalize_literals(literal_run_len_3hm);

    spill_b_.flush();

    state_ = DPFlateState::BUILD_TREE;
}

auto DPFlate::handleBuildTree(AlgorithmStatus& status, bool is_last_chunk) -> void {
    (void)is_last_chunk;
    if (huff_build_tree_and_write_trees(status)) {
        state_ = DPFlateState::EMIT_TOKENS;
        emitted_tokens_ = 0;
    }
}

auto DPFlate::handleEmitTokens(AlgorithmStatus& status, bool is_last_chunk) -> void {
    (void)is_last_chunk;
    huff_emit_token_stream(status);
}

auto DPFlate::handle(AlgorithmStatus& status, bool is_last_chunk) -> void {
    while (true) {
        (this->*kStateHandlers[static_cast<size_t>(state_)])(status, is_last_chunk);

        if (status.need_input || status.need_output || status.done) {
            return;
        }
    }
}

void DPFlate::getLengthCode(size_t length, uint16_t& code, uint8_t& extra_bits,
                            uint16_t& extra_val) {
    if (length <= 10) {
        code = static_cast<uint16_t>(257 + length - 3);
        extra_bits = 0;
        extra_val = 0;
    } else if (length <= 18) {
        extra_bits = 1;
        code = static_cast<uint16_t>(265 + (length - 11) / 2);
        extra_val = static_cast<uint16_t>((length - 11) % 2);
    } else if (length <= 34) {
        extra_bits = 2;
        code = static_cast<uint16_t>(269 + (length - 19) / 4);
        extra_val = static_cast<uint16_t>((length - 19) % 4);
    } else if (length <= 66) {
        extra_bits = 3;
        code = static_cast<uint16_t>(273 + (length - 35) / 8);
        extra_val = static_cast<uint16_t>((length - 35) % 8);
    } else if (length <= 130) {
        extra_bits = 4;
        code = static_cast<uint16_t>(277 + (length - 67) / 16);
        extra_val = static_cast<uint16_t>((length - 67) % 16);
    } else if (length <= 257) {
        extra_bits = 5;
        code = static_cast<uint16_t>(281 + (length - 131) / 32);
        extra_val = static_cast<uint16_t>((length - 131) % 32);
    } else if (length == 258) {
        code = 285;
        extra_bits = 0;
        extra_val = 0;
    }
}

void DPFlate::getDistCode(size_t dist, uint8_t& code, uint8_t& extra_bits,
                          uint16_t& extra_val) {
    dist -= 1;
    if (dist < 4) {
        code = static_cast<uint8_t>(dist);
        extra_bits = 0;
        extra_val = 0;
        return;
    }

    uint8_t msb = 0;
    size_t temp = dist >> 2;
    while (temp) {
        temp >>= 1;
        msb++;
    }

    extra_bits = msb;
    code = static_cast<uint8_t>((msb << 1) + 2 + ((dist >> msb) & 1));
    extra_val = static_cast<uint16_t>(dist & ((1 << msb) - 1));
}

void DPFlate::huff_reset_entropy_tables() {
    freq_map_.assign(DEFLATE_ALPHABET_SIZE, 0);
    dist_freq_.assign(DISTANCE_DICTIONARY_SIZE, 0);
    huffman_tree_.reset();
    dist_tree_.reset();
    huffman_tree_3hm_.reset();
    dictionary_.clear();
    dist_dictionary_.clear();

    literal_freq_3hm_.assign(256, 0);
    offset_count_3hm_ = static_cast<size_t>(1) << huffman_offset_chunk_bits_;
    length_count_3hm_ = static_cast<size_t>(1) << huffman_length_chunk_bits_;
    offset_freq_3hm_.assign(offset_count_3hm_, 0);
    length_freq_3hm_.assign(length_count_3hm_, 0);
}

void DPFlate::huff_backtrack_accumulate_token(uint16_t length, uint16_t offset,
                                              size_t& literal_run_len_3hm) {
    Token t;
    if (use_3hfmtree_) {
        if (length == 0) {
            t.is_literal = true;
            t.code = offset;
            literal_freq_3hm_[offset]++;
            literal_run_len_3hm++;
        } else {
            t.is_literal = false;
            getLengthCode(length, t.code, t.length_extra_bits, t.length_extra_val);
            getDistCode(offset, t.dist_code, t.dist_extra_bits, t.dist_extra_val);
            if (literal_run_len_3hm > 0) {
                const size_t max_run = length_count_3hm_ - 1;
                size_t remaining = literal_run_len_3hm;
                while (remaining > 0) {
                    const size_t chunk = (remaining > max_run) ? max_run : remaining;
                    offset_freq_3hm_[0]++;
                    length_freq_3hm_[chunk]++;
                    remaining -= chunk;
                }
                literal_run_len_3hm = 0;
            }
            const size_t ob = offset_bits_3hm();
            const size_t lb = length_bits_3hm();
            const size_t off_chunks =
                (ob + huffman_offset_chunk_bits_ - 1) / huffman_offset_chunk_bits_;
            const size_t len_chunks =
                (lb + huffman_length_chunk_bits_ - 1) / huffman_length_chunk_bits_;
            const size_t mask_ob = (size_t{1} << huffman_offset_chunk_bits_) - 1;
            const size_t mask_lb = (size_t{1} << huffman_length_chunk_bits_) - 1;
            for (size_t i = 0; i < off_chunks; ++i) {
                const size_t chunk =
                    (static_cast<size_t>(offset) >> (i * huffman_offset_chunk_bits_)) & mask_ob;
                offset_freq_3hm_[chunk]++;
            }
            for (size_t i = 0; i < len_chunks; ++i) {
                const size_t chunk =
                    (static_cast<size_t>(length) >> (i * huffman_length_chunk_bits_)) & mask_lb;
                length_freq_3hm_[chunk]++;
            }
        }
    } else {
        if (length == 0) {
            t.is_literal = true;
            t.code = offset;
        } else {
            t.is_literal = false;
            getLengthCode(length, t.code, t.length_extra_bits, t.length_extra_val);
            getDistCode(offset, t.dist_code, t.dist_extra_bits, t.dist_extra_val);
        }
    }
    freq_map_[t.code]++;
    if (!t.is_literal) {
        dist_freq_[t.dist_code]++;
    }
}

void DPFlate::huff_backtrack_finalize_literals(size_t& literal_run_len_3hm) {
    if (!use_3hfmtree_ || literal_run_len_3hm == 0) {
        return;
    }
    const size_t max_run = length_count_3hm_ - 1;
    size_t remaining = literal_run_len_3hm;
    while (remaining > 0) {
        const size_t chunk = (remaining > max_run) ? max_run : remaining;
        offset_freq_3hm_[0]++;
        length_freq_3hm_[chunk]++;
        remaining -= chunk;
    }
}

bool DPFlate::huff_build_tree_and_write_trees(AlgorithmStatus& st) {
    if (use_3hfmtree_) {
        huffman_tree_3hm_ = std::make_unique<HuffmanTree3HM>();
        huffman_tree_3hm_->buildTrees(
            literal_freq_3hm_, offset_freq_3hm_, length_freq_3hm_, offset_count_3hm_,
            length_count_3hm_, offset_bits_3hm(), length_bits_3hm(), huffman_offset_chunk_bits_,
            huffman_length_chunk_bits_);
        if (!writer_.ensureSpace(huffman_tree_3hm_->getTreeSize())) {
            st.need_output = true;
            return false;
        }
        huffman_tree_3hm_->serialize(writer_);
        return true;
    }
    freq_map_[256] = 1;
    huffman_tree_ = std::make_unique<HuffmanTree>(freq_map_, DEFLATE_ALPHABET_SIZE, DEFLATE_SYMBOL_BITS);
    dist_tree_ =
        std::make_unique<HuffmanTree>(dist_freq_, DISTANCE_DICTIONARY_SIZE, DISTANCE_SYMBOL_BITS);
    dictionary_ = huffman_tree_->buildDictionary();
    dist_dictionary_ = dist_tree_->buildDictionary();
    if (!writer_.ensureSpace(huffman_tree_->getTreeSize() + dist_tree_->getTreeSize())) {
        st.need_output = true;
        return false;
    }
    huffman_tree_->serializeTree(writer_);
    dist_tree_->serializeTree(writer_);
    return true;
}

bool DPFlate::huff_emit_token_stream(AlgorithmStatus& st) {
    PackedDpLinkBackwardWindow readerB(temp_file_B_, spill_spec_);
    if (use_3hfmtree_) {
        std::vector<uint8_t> literal_run;
        const size_t max_run = length_count_3hm_ - 1;
        while (emitted_tokens_ < total_tokens_) {
            if ((emitted_tokens_ & 0xFFFF) == 0 && algorithm::g_cancel_callback &&
                algorithm::g_cancel_callback()) {
                throw std::runtime_error("cancelled");
            }
            if (!writer_.ensureSpace(6)) {
                st.need_output = true;
                return false;
            }
            const uint64_t idx = total_tokens_ - 1 - emitted_tokens_;
            uint16_t raw_len = 0;
            uint16_t raw_off = 0;
            readerB.readPair(idx, raw_len, raw_off);
            if (raw_len == 0) {
                literal_run.push_back(static_cast<uint8_t>(raw_off));
            } else {
                if (!literal_run.empty()) {
                    size_t remaining = literal_run.size();
                    size_t run_pos = 0;
                    while (remaining > 0) {
                        const size_t chunk = (remaining > max_run) ? max_run : remaining;
                        huffman_tree_3hm_->encodeRunHeader(static_cast<uint16_t>(chunk), writer_);
                        for (size_t i = 0; i < chunk; ++i) {
                            huffman_tree_3hm_->encodeLiteral(literal_run[run_pos + i], writer_);
                        }
                        run_pos += chunk;
                        remaining -= chunk;
                    }
                    literal_run.clear();
                }
                huffman_tree_3hm_->encodeMatch(raw_off, raw_len, writer_);
            }
            emitted_tokens_++;
        }
        if (!literal_run.empty()) {
            if (!writer_.ensureSpace(6)) {
                st.need_output = true;
                return false;
            }
            size_t remaining = literal_run.size();
            size_t run_pos = 0;
            while (remaining > 0) {
                const size_t chunk = (remaining > max_run) ? max_run : remaining;
                huffman_tree_3hm_->encodeRunHeader(static_cast<uint16_t>(chunk), writer_);
                for (size_t i = 0; i < chunk; ++i) {
                    huffman_tree_3hm_->encodeLiteral(literal_run[run_pos + i], writer_);
                }
                run_pos += chunk;
                remaining -= chunk;
            }
            literal_run.clear();
        }
        writer_.flush();
        st.done = true;
        return true;
    }

    while (emitted_tokens_ < total_tokens_) {
        if ((emitted_tokens_ & 0xFFFF) == 0 && algorithm::g_cancel_callback &&
            algorithm::g_cancel_callback()) {
            throw std::runtime_error("cancelled");
        }
        if (!writer_.ensureSpace(6)) {
            st.need_output = true;
            return false;
        }
        const uint64_t idx = total_tokens_ - 1 - emitted_tokens_;
        uint16_t raw_len = 0;
        uint16_t raw_off = 0;
        readerB.readPair(idx, raw_len, raw_off);
        Token token;
        if (raw_len == 0) {
            token.is_literal = true;
            token.code = raw_off;
        } else {
            token.is_literal = false;
            getLengthCode(raw_len, token.code, token.length_extra_bits, token.length_extra_val);
            getDistCode(raw_off, token.dist_code, token.dist_extra_bits, token.dist_extra_val);
        }
        const auto& main_code = dictionary_[token.code];
        for (int i = main_code.length - 1; i >= 0; i--) {
            writer_.writeBit((main_code.code >> i) & 1);
        }
        if (!token.is_literal) {
            for (int i = 0; i < token.length_extra_bits; i++) {
                writer_.writeBit((token.length_extra_val >> i) & 1);
            }
            const auto& dist_code = dist_dictionary_[token.dist_code];
            for (int i = dist_code.length - 1; i >= 0; i--) {
                writer_.writeBit((dist_code.code >> i) & 1);
            }
            for (int i = 0; i < token.dist_extra_bits; i++) {
                writer_.writeBit((token.dist_extra_val >> i) & 1);
            }
        }
        emitted_tokens_++;
    }
    if (!writer_.ensureSpace(6)) {
        st.need_output = true;
        return false;
    }
    const auto& eof_code = dictionary_[256];
    for (int i = eof_code.length - 1; i >= 0; i--) {
        writer_.writeBit((eof_code.code >> i) & 1);
    }
    writer_.flush();
    st.done = true;
    return true;
}

}  // namespace compressor::algorithm
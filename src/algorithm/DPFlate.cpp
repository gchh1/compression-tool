#include "DPFlate.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
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
#include "DebugLog.hpp"
#include "DPFlateBin64kDebug.hpp"
#include "DPFlateTrace.hpp"
#include "TopMatch.hpp"
#include "HuffmanTree.hpp"
#include "KMPMatcher.hpp"

namespace compressor::algorithm {

// === DEBUG_BLOCK_BEGIN (可删除) ===
static auto dbg_temp_file_size_bytes(const TempFile& tf) -> unsigned long long {
    std::error_code ec;
    const auto sz = std::filesystem::file_size(tf.path_, ec);
    return ec ? 0ULL : static_cast<unsigned long long>(sz);
}
// === DEBUG_BLOCK_END ===

DPFlate::DPFlate(size_t search_size, size_t lookahead_size, size_t min_match,
                 size_t dp_top, size_t dp_sub_match_max)
    : SEARCH_SIZE(search_size),
      LOOKAHEAD_SIZE(lookahead_size),
      MIN_MATCH(min_match),
      DP_TOP(dp_top),
      DP_SUB_MATCH_MAX(dp_sub_match_max) {
    DPFlateBin64kDebug::init_once();
    DPFLATE_BIN64K_LOG("CTOR", "search=%zu look=%zu min_match=%zu", search_size, lookahead_size,
                        min_match);
    reset();
}

auto DPFlate::reset(void) -> void {
    DPFLATE_BIN64K_LOG("RESET", "enter use_3hm=%d", use_3hfmtree_ ? 1 : 0);
    temp_file_A_.recreate();
    temp_file_B_.recreate();
    temp_tokens_a_.rebind(&temp_file_A_);
    temp_tokens_b_.rebind(&temp_file_B_);

    input_buffer_.clear();
    DPFLATE_BIN64K_LOG("RESET", "input_buffer.clear cap=%zu", input_buffer_.capacity());

    streaming_dp_.reset(LOOKAHEAD_SIZE);
    DPFLATE_BIN64K_LOG("RESET", "streaming_dp.reset lookahead=%zu", LOOKAHEAD_SIZE);

    head_.assign(std::max(SEARCH_SIZE, size_t{1}), UINT32_MAX);
    prev_buf_.clear();
    DPFLATE_BIN64K_LOG("RESET", "head.assign size=%zu prev_buf.clear", head_.size());

    window_abs_pos_ = 0;
    current_i_ = 0;
    total_in_len_ = 0;

    total_tokens_ = 0;
    emitted_tokens_ = 0;
    emit_literal_run_3hm_.clear();
    DPFLATE_BIN64K_LOG("RESET", "emit_literal_run_3hm.clear");

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
    if (use_3hfmtree_) {
        if (const char* only = std::getenv("WEBCOMPRESS_DPFLATE_BIN64K_ONLY");
            only && only[0] == '1') {
            DPFlateBin64kDebug::arm_session(64u * 1024u, true);
        }
    }
    DPFLATE_TRACE_EVENT("RESET", "search=%zu look=%zu min_match=%zu use_3hm=%d use_flag=%d",
                        SEARCH_SIZE, LOOKAHEAD_SIZE, MIN_MATCH, use_3hfmtree_ ? 1 : 0,
                        use_flag_encoding_ ? 1 : 0);
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
    DPFLATE_BIN64K_LOG("RESEED_HASH", "end_exclusive=%zu head=%zu prev=%zu buf=%zu", end_exclusive,
                        head_.size(), prev_buf_.size(), input_buffer_.size());
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
 * 与 ``LZDP_Streaming`` 的 Phase1 同构；缩写见 ``docs/缩写对照表.md``。
 */
void dpflate_collect_input_one_index(DPFlate& self, size_t pos_idx, uint32_t abs_pos) {
    auto& cur = self.streaming_dp_.cell_at(abs_pos);
    // Snapshot before further cell_at() calls — next_.resize() invalidates ``cur``.
    const uint32_t cur_cost = cur.cost;
    const uint16_t link_len = cur.length;
    const uint16_t link_off = cur.offset;

    auto& next_lit = self.streaming_dp_.cell_at(abs_pos + 1);
    if (cur_cost + self.lit_cost_ < next_lit.cost) {
        next_lit.cost = cur_cost + self.lit_cost_;
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
                if (kr.offset == 0) {
                    continue;
                }
                auto& nm = self.streaming_dp_.cell_at(
                    abs_pos + static_cast<uint32_t>(kr.length));
                if (cur_cost + self.match_cost_ < nm.cost) {
                    nm.cost = cur_cost + self.match_cost_;
                    nm.length = static_cast<uint16_t>(kr.length);
                    nm.offset = static_cast<uint16_t>(kr.offset);
                }
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
                auto& nm = self.streaming_dp_.cell_at(
                    abs_pos + static_cast<uint32_t>(e.length));
                if (cur_cost + self.match_cost_ < nm.cost) {
                    nm.cost = cur_cost + self.match_cost_;
                    nm.length = e.length;
                    nm.offset = e.offset;
                }
            }
        }
    }

    self.temp_tokens_a_.writeAt(abs_pos, link_len, link_off);
    if ((abs_pos & 0x3FFFu) == 0 || (abs_pos != 0 && (abs_pos % 3072u) == 0)) {
        DPFLATE_BIN64K_LOG("COLLECT_IDX", "abs_pos=%u len=%u off=%u buf=%zu", abs_pos, link_len,
                            link_off, self.input_buffer_.size());
        if (pos_idx < self.input_buffer_.size()) {
            DPFlateBin64kDebug::log_hex("COLLECT_BYTE", "at_abs", &self.input_buffer_[pos_idx], 1);
        }
    }
}

auto DPFlate::handleCollectInput(AlgorithmStatus& status, bool is_last_chunk) -> void {
    size_t remain = reader_.getRemainSize();
    DPFlateBin64kDebug::try_arm_from_buffer(use_3hfmtree_, input_buffer_.size() + remain,
                                            window_abs_pos_, is_last_chunk);
    DPFLATE_BIN64K_LOG("COLLECT_ENTER", "remain=%zu buf=%zu cur_i=%zu win_abs=%u is_last=%d",
                        remain, input_buffer_.size(), current_i_, window_abs_pos_,
                        is_last_chunk ? 1 : 0);
    DPFLATE_TRACE_EVENT("COLLECT_ENTER", "remain=%zu buf=%zu cur_i=%zu win_abs=%u is_last=%d",
                        remain, input_buffer_.size(), current_i_, window_abs_pos_,
                        is_last_chunk ? 1 : 0);
    // === DEBUG_BLOCK_BEGIN (可删除) ===
    static int dbg_collect_cnt = 0;
    if (++dbg_collect_cnt <= 50) {
        DEBUG_LOG("[DPFlate] handleCollectInput enter: input_buffer=%zu remain=%zu current_i=%zu "
                  "window_abs=%llu is_last=%d dp_slots=%zu search=%zu lookahead=%zu",
                  input_buffer_.size(), remain, current_i_,
                  static_cast<unsigned long long>(window_abs_pos_), is_last_chunk,
                  LOOKAHEAD_SIZE, SEARCH_SIZE, LOOKAHEAD_SIZE);
    }
    // === DEBUG_BLOCK_END ===

    if (remain > 0) {
        size_t old_len = input_buffer_.size();
        input_buffer_.resize(old_len + remain);
        DPFLATE_BIN64K_LOG("COLLECT_READ", "input_buffer.resize %zu->%zu cap=%zu", old_len,
                            input_buffer_.size(), input_buffer_.capacity());
        size_t copied = reader_.readBytes(input_buffer_.data() + old_len, remain);
        if (copied < remain) {
            input_buffer_.resize(old_len + copied);
            DPFLATE_BIN64K_LOG("COLLECT_READ", "input_buffer.shrink after read copied=%zu",
                                copied);
        }
        if (copied > 0) {
            DPFlateBin64kDebug::log_hex("BUF_IN", "read_tail", input_buffer_.data() + old_len,
                                        copied);
        }
    }

    if (prev_buf_.size() != input_buffer_.size()) {
        const size_t old_prev = prev_buf_.size();
        prev_buf_.resize(input_buffer_.size(), UINT32_MAX);
        DPFLATE_BIN64K_LOG("COLLECT_PREV", "prev_buf.resize %zu->%zu cap=%zu", old_prev,
                            prev_buf_.size(), prev_buf_.capacity());
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
        DPFLATE_BIN64K_LOG("COLLECT_PROC", "processable=%zu cur_i=%zu win_abs=%u", processable,
                            current_i_, window_abs_pos_);
        if (!input_buffer_.empty()) {
            DPFlateBin64kDebug::log_hex("BUF_IN", "buf_head", input_buffer_.data(),
                                        input_buffer_.size());
        }
        if (!prev_buf_.empty()) {
            DPFlateBin64kDebug::log_u32_slice("BUF_PREV", "prev_head", prev_buf_.data(),
                                              std::min(prev_buf_.size(), size_t{16}), 0);
        }
        if (!head_.empty()) {
            DPFlateBin64kDebug::log_u32_slice("BUF_HEAD", "head_slots", head_.data(),
                                              std::min(head_.size(), size_t{8}), 0);
        }
        streaming_dp_.debug_dump_cells("before_batch");
        // === DEBUG_BLOCK_BEGIN (可删除) ===
        static int dbg_processable_cnt = 0;
        if (++dbg_processable_cnt <= 50) {
            DEBUG_LOG("[DPFlate] handleCollectInput processable=%zu buffer=%zu current_i=%zu "
                      "tempA_before=%llu prev_buf=%zu",
                      processable, input_buffer_.size(), current_i_,
                      dbg_temp_file_size_bytes(temp_file_A_), prev_buf_.size());
        }
        // === DEBUG_BLOCK_END ===

        for (size_t k = 0; k < processable; ++k) {
            if ((k & size_t{4095}) == 0 && algorithm::g_cancel_callback &&
                algorithm::g_cancel_callback()) {
                throw std::runtime_error("cancelled");
            }
            size_t pos_idx = current_i_ + k;
            uint32_t abs_pos = window_abs_pos_ + static_cast<uint32_t>(pos_idx);

            dpflate_collect_input_one_index(*this, pos_idx, abs_pos);
        }
        streaming_dp_.debug_dump_cells("after_batch");
        current_i_ += processable;
        const uint32_t commit_until_abs =
            window_abs_pos_ + static_cast<uint32_t>(current_i_);
        streaming_dp_.rotate(commit_until_abs);
        DPFLATE_TRACE_EVENT(
            "COLLECT_BATCH", "processable=%zu cur_i=%zu win_abs=%u buf=%zu is_last=%d",
            processable, current_i_, window_abs_pos_, input_buffer_.size(),
            is_last_chunk ? 1 : 0);
        if (algorithm::g_cancel_callback && algorithm::g_cancel_callback()) {
            throw std::runtime_error("cancelled");
        }
    }
    
    if (!is_last_chunk) {
        size_t next_abs_pos = window_abs_pos_ + current_i_;
        size_t keep_start_abs = (next_abs_pos > SEARCH_SIZE) ? (next_abs_pos - SEARCH_SIZE) : 0;
        size_t keep_start_idx = keep_start_abs - window_abs_pos_;
        
        if (keep_start_idx > 0) {
            DPFLATE_BIN64K_LOG("COLLECT_SLIDE", "keep_start_abs=%zu keep_start_idx=%zu buf=%zu",
                                keep_start_abs, keep_start_idx, input_buffer_.size());
            streaming_dp_.prune_before(static_cast<uint32_t>(keep_start_abs));
            std::vector<uint8_t> new_buf(input_buffer_.begin() + keep_start_idx, input_buffer_.end());
            std::vector<uint32_t> new_prev(prev_buf_.begin() + keep_start_idx, prev_buf_.end());
            DPFLATE_BIN64K_LOG("COLLECT_SLIDE", "new_buf.size=%zu new_prev.size=%zu (iterator copy)",
                                new_buf.size(), new_prev.size());
            input_buffer_ = std::move(new_buf);
            prev_buf_ = std::move(new_prev);
            DPFLATE_BIN64K_LOG("COLLECT_SLIDE",
                                "moved input_buffer=%zu prev_buf=%zu cap_in=%zu cap_prev=%zu",
                                input_buffer_.size(), prev_buf_.size(), input_buffer_.capacity(),
                                prev_buf_.capacity());
            if (!input_buffer_.empty()) {
                DPFlateBin64kDebug::log_hex("BUF_IN", "after_slide", input_buffer_.data(),
                                            input_buffer_.size());
            }
            if (!prev_buf_.empty()) {
                DPFlateBin64kDebug::log_u32_slice("BUF_PREV", "after_slide", prev_buf_.data(),
                                                  std::min(prev_buf_.size(), size_t{16}), 0);
            }
            window_abs_pos_ = static_cast<uint32_t>(keep_start_abs);
            current_i_ -= keep_start_idx;
            if (match_engine_ == 1) {
                reseedHashChainPrefix(current_i_);
            }
        }
        DPFLATE_TRACE_SNAPSHOT(*this, "COLLECT_PAUSE");
        status.need_input = true;
    } else {
        total_in_len_ = window_abs_pos_ + static_cast<uint32_t>(current_i_);
        DPFlateBin64kDebug::arm_session(total_in_len_, use_3hfmtree_);
        DPFLATE_BIN64K_LOG("COLLECT_DONE", "total_in_len=%u buf=%zu", total_in_len_,
                            input_buffer_.size());
        streaming_dp_.rotate(total_in_len_);

        StreamingDpCell& end_cell = streaming_dp_.cell_at(total_in_len_);
        temp_tokens_a_.writeAt(total_in_len_, end_cell.length, end_cell.offset);
        temp_file_A_.flush();
        DPFLATE_BIN64K_LOG("COLLECT_DONE", "tempA_bytes=%llu end_link len=%u off=%u",
                            dbg_temp_file_size_bytes(temp_file_A_), end_cell.length, end_cell.offset);
        for (uint32_t probe : {0u, 16384u, 32768u, 49152u, total_in_len_}) {
            uint16_t pl = 0;
            uint16_t po = 0;
            temp_tokens_a_.readAt(probe, pl, po);
            DPFLATE_BIN64K_LOG("TEMP_A_PROBE", "index=%u len=%u off=%u", probe, pl, po);
        }
        if (!input_buffer_.empty()) {
            DPFlateBin64kDebug::log_hex("BUF_IN", "collect_done", input_buffer_.data(),
                                        input_buffer_.size());
        }

        // === DEBUG_BLOCK_BEGIN (可删除) ===
        static int dbg_collect_done_cnt = 0;
        if (++dbg_collect_done_cnt <= 50) {
            DEBUG_LOG("[DPFlate] handleCollectInput final: total_in_len=%u final_link=(len=%u off=%u) "
                      "tempA=%llu input_buffer=%zu",
                      total_in_len_, end_cell.length, end_cell.offset,
                      dbg_temp_file_size_bytes(temp_file_A_), input_buffer_.size());
        }
        // === DEBUG_BLOCK_END ===

        DPFLATE_TRACE_SNAPSHOT(*this, "COLLECT_DONE");
        state_ = DPFlateState::BACKTRACK;
    }
}

auto DPFlate::handleBacktrack(AlgorithmStatus& status, bool is_last_chunk) -> void {
    (void)is_last_chunk;
    if (algorithm::g_cancel_callback && algorithm::g_cancel_callback()) {
        throw std::runtime_error("cancelled");
    }

    uint32_t cur = total_in_len_;
    size_t literal_run_len_3hm = 0;

    // === DEBUG_BLOCK_BEGIN (可删除) ===
    static int dbg_backtrack_enter_cnt = 0;
    if (++dbg_backtrack_enter_cnt <= 50) {
        DEBUG_LOG("[DPFlate] handleBacktrack enter: total_in_len=%u tempA=%llu use_3hm=%d",
                  total_in_len_, dbg_temp_file_size_bytes(temp_file_A_), use_3hfmtree_);
    }
    // === DEBUG_BLOCK_END ===

    DPFLATE_BIN64K_LOG("BACKTRACK", "enter total_in_len=%u", total_in_len_);
    uint64_t backtrack_out_bytes = 0;
    uint64_t backtrack_literal_tokens = 0;
    uint64_t backtrack_match_tokens = 0;
    while (cur > 0) {
        if ((cur & 0xFFFF) == 0 && algorithm::g_cancel_callback && algorithm::g_cancel_callback()) {
            throw std::runtime_error("cancelled");
        }

        uint16_t length = 0;
        uint16_t offset = 0;
        temp_tokens_a_.readAt(cur, length, offset);
        if (length == 0) {
            ++backtrack_literal_tokens;
        } else {
            ++backtrack_match_tokens;
        }
        backtrack_out_bytes += (length == 0) ? 1ULL : static_cast<uint64_t>(length);
        if ((cur & 0x3FFFu) == 0) {
            DPFLATE_BIN64K_LOG("BACKTRACK_STEP", "cur=%u len=%u off=%u tokens=%llu", cur, length,
                                offset, static_cast<unsigned long long>(total_tokens_));
        }

        // === DEBUG_BLOCK_BEGIN (可删除) ===
        static int dbg_backtrack_token_cnt = 0;
        if (++dbg_backtrack_token_cnt <= 50) {
            DEBUG_LOG("[DPFlate] handleBacktrack token: cur=%u len=%u off=%u tokens=%llu",
                      cur, length, offset, static_cast<unsigned long long>(total_tokens_));
        }
        // === DEBUG_BLOCK_END ===

        temp_tokens_b_.append(length, offset);
        total_tokens_++;

        huff_backtrack_accumulate_token(length, offset, literal_run_len_3hm);

        if (length == 0) {
            cur -= 1;
        } else {
            cur -= length;
        }
    }

    huff_backtrack_finalize_literals(literal_run_len_3hm);

    temp_file_B_.flush();
    DPFLATE_BIN64K_LOG(
        "BACKTRACK",
        "done tokens=%llu literal_tok=%llu match_tok=%llu tempB_bytes=%llu out_bytes=%llu",
        static_cast<unsigned long long>(total_tokens_),
        static_cast<unsigned long long>(backtrack_literal_tokens),
        static_cast<unsigned long long>(backtrack_match_tokens),
        dbg_temp_file_size_bytes(temp_file_B_),
        static_cast<unsigned long long>(backtrack_out_bytes));

    // === DEBUG_BLOCK_BEGIN (可删除) ===
    static int dbg_backtrack_done_cnt = 0;
    if (++dbg_backtrack_done_cnt <= 50) {
        DEBUG_LOG("[DPFlate] handleBacktrack done: total_tokens=%llu out_bytes=%llu "
                  "expected=%u tempA=%llu tempB=%llu",
                  static_cast<unsigned long long>(total_tokens_),
                  static_cast<unsigned long long>(backtrack_out_bytes), total_in_len_,
                  dbg_temp_file_size_bytes(temp_file_A_),
                  dbg_temp_file_size_bytes(temp_file_B_));
    }
    // === DEBUG_BLOCK_END ===

    DPFLATE_TRACE_SNAPSHOT(*this, "BACKTRACK_DONE");
    state_ = DPFlateState::BUILD_TREE;
}

auto DPFlate::handleBuildTree(AlgorithmStatus& status, bool is_last_chunk) -> void {
    (void)is_last_chunk;
    // === DEBUG_BLOCK_BEGIN (可删除) ===
    static int dbg_build_enter_cnt = 0;
    if (++dbg_build_enter_cnt <= 50) {
        DEBUG_LOG("[DPFlate] handleBuildTree enter: use_3hm=%d writer_bytes=%zu total_tokens=%llu",
                  use_3hfmtree_, writer_.getBytesWritten(),
                  static_cast<unsigned long long>(total_tokens_));
    }
    // === DEBUG_BLOCK_END ===

    if (huff_build_tree_and_write_trees(status)) {
        state_ = DPFlateState::EMIT_TOKENS;
        emitted_tokens_ = 0;
        // === DEBUG_BLOCK_BEGIN (可删除) ===
        static int dbg_build_done_cnt = 0;
        if (++dbg_build_done_cnt <= 50) {
            DEBUG_LOG("[DPFlate] handleBuildTree done: writer_bytes=%zu need_output=%d",
                      writer_.getBytesWritten(), status.need_output);
        }
        // === DEBUG_BLOCK_END ===
        DPFLATE_TRACE_SNAPSHOT(*this, "BUILD_TREE_DONE");
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
    DPFLATE_BIN64K_LOG("HUFF_RESET", "assign freq_map=%zu dist_freq=%zu 3hm off=%zu len=%zu",
                        DEFLATE_ALPHABET_SIZE, DISTANCE_DICTIONARY_SIZE, offset_count_3hm_,
                        length_count_3hm_);
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
    DPFLATE_BIN64K_LOG("HUFF_RESET", "literal_freq_3hm=%zu offset_freq_3hm=%zu length_freq_3hm=%zu",
                        literal_freq_3hm_.size(), offset_freq_3hm_.size(), length_freq_3hm_.size());
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
        // === DEBUG_BLOCK_BEGIN (可删除) ===
        static int dbg_3hm_tree_cnt = 0;
        if (++dbg_3hm_tree_cnt <= 50) {
            DEBUG_LOG("[DPFlate] huff_build_tree_and_write_trees 3HM: offset_bits=%zu length_bits=%zu "
                      "offset_chunk=%zu length_chunk=%zu offset_count=%zu length_count=%zu tree_bits=%zu",
                      offset_bits_3hm(), length_bits_3hm(), huffman_offset_chunk_bits_,
                      huffman_length_chunk_bits_, offset_count_3hm_, length_count_3hm_,
                      huffman_tree_3hm_->getTreeSize());
        }
        // === DEBUG_BLOCK_END ===
        if (!writer_.ensureSpace(8 + huffman_tree_3hm_->getTreeSize())) {
            st.need_output = true;
            return false;
        }
        writer_.writeBits(0x33, 8);
        huffman_tree_3hm_->serialize(writer_);
        return true;
    }
    freq_map_[256] = 1;
    huffman_tree_ = std::make_unique<HuffmanTree>(freq_map_, DEFLATE_ALPHABET_SIZE, DEFLATE_SYMBOL_BITS);
    dist_tree_ =
        std::make_unique<HuffmanTree>(dist_freq_, DISTANCE_DICTIONARY_SIZE, DISTANCE_SYMBOL_BITS);
    dictionary_ = huffman_tree_->buildDictionary();
    dist_dictionary_ = dist_tree_->buildDictionary();
    // === DEBUG_BLOCK_BEGIN (可删除) ===
    static int dbg_flate_tree_cnt = 0;
    if (++dbg_flate_tree_cnt <= 50) {
        DEBUG_LOG("[DPFlate] huff_build_tree_and_write_trees FLATE: main_tree_bits=%zu "
                  "dist_tree_bits=%zu total_tokens=%llu",
                  huffman_tree_->getTreeSize(), dist_tree_->getTreeSize(),
                  static_cast<unsigned long long>(total_tokens_));
    }
    // === DEBUG_BLOCK_END ===
    if (!writer_.ensureSpace(8 + huffman_tree_->getTreeSize() + dist_tree_->getTreeSize())) {
        st.need_output = true;
        return false;
    }
    writer_.writeBits(0x46, 8);
    huffman_tree_->serializeTree(writer_);
    dist_tree_->serializeTree(writer_);
    return true;
}

bool DPFlate::huff_emit_token_stream(AlgorithmStatus& st) {
    if (total_tokens_ == 0) {
        st.done = true;
        return false;
    }
    // === DEBUG_BLOCK_BEGIN (可删除) ===
    static int dbg_emit_enter_cnt = 0;
    if (++dbg_emit_enter_cnt <= 50) {
        DEBUG_LOG("[DPFlate] handleEmitTokens enter: total_tokens=%llu emitted=%llu writer_bytes=%zu use_3hm=%d",
                  static_cast<unsigned long long>(total_tokens_),
                  static_cast<unsigned long long>(emitted_tokens_),
                  writer_.getBytesWritten(), use_3hfmtree_);
    }
    // === DEBUG_BLOCK_END ===

    if (use_3hfmtree_) {
        const size_t max_run = length_count_3hm_ - 1;
        DPFLATE_BIN64K_LOG("EMIT_3HM", "enter total_tokens=%llu max_run=%zu",
                            static_cast<unsigned long long>(total_tokens_), max_run);

        while (emitted_tokens_ < total_tokens_) {
            if ((emitted_tokens_ & 0xFFFF) == 0 && algorithm::g_cancel_callback &&
                algorithm::g_cancel_callback()) {
                throw std::runtime_error("cancelled");
            }
            if (!writer_.ensureSpace(64)) {
                st.need_output = true;
                return false;
            }
            const uint64_t idx = total_tokens_ - 1 - emitted_tokens_;
            uint16_t raw_len = 0;
            uint16_t raw_off = 0;
            temp_tokens_b_.readBySeqIndex(idx, raw_len, raw_off);

            // === DEBUG_BLOCK_BEGIN (可删除) ===
            static int dbg_emit_3hm_token_cnt = 0;
            if (++dbg_emit_3hm_token_cnt <= 50) {
                DEBUG_LOG("[DPFlate] handleEmitTokens 3HM token: idx=%llu len=%u off=%u literal_run=%zu writer_bytes=%zu",
                          static_cast<unsigned long long>(idx), raw_len, raw_off,
                          emit_literal_run_3hm_.size(), writer_.getBytesWritten());
            }
            // === DEBUG_BLOCK_END ===

            if (raw_len == 0) {
                emit_literal_run_3hm_.push_back(static_cast<uint8_t>(raw_off));
                if ((emit_literal_run_3hm_.size() & 0x3FFFu) == 0) {
                    DPFLATE_BIN64K_LOG("EMIT_3HM", "literal_run.push_back size=%zu cap=%zu",
                                        emit_literal_run_3hm_.size(),
                                        emit_literal_run_3hm_.capacity());
                }
            } else {
                if (!emit_literal_run_3hm_.empty()) {
                    DPFLATE_BIN64K_LOG("EMIT_3HM", "literal_run.flush size=%zu before match",
                                        emit_literal_run_3hm_.size());
                    size_t remaining = emit_literal_run_3hm_.size();
                    size_t run_pos = 0;
                    while (remaining > 0) {
                        const size_t chunk = (remaining > max_run) ? max_run : remaining;
                        huffman_tree_3hm_->encodeRunHeader(static_cast<uint16_t>(chunk), writer_);
                        for (size_t i = 0; i < chunk; ++i) {
                            huffman_tree_3hm_->encodeLiteral(emit_literal_run_3hm_[run_pos + i], writer_);
                        }
                        run_pos += chunk;
                        remaining -= chunk;
                    }
                    emit_literal_run_3hm_.clear();
                    DPFLATE_BIN64K_LOG("EMIT_3HM", "literal_run.clear after flush");
                }
                huffman_tree_3hm_->encodeMatch(raw_off, raw_len, writer_);
            }
            if ((emitted_tokens_ & 0x3FFFu) == 0) {
                DPFLATE_BIN64K_LOG("EMIT_3HM", "progress emitted=%llu writer=%zu",
                                    static_cast<unsigned long long>(emitted_tokens_),
                                    writer_.getBytesWritten());
            }
            emitted_tokens_++;
        }
        if (!emit_literal_run_3hm_.empty()) {
            DPFLATE_BIN64K_LOG("EMIT_3HM", "literal_run.tail size=%zu", emit_literal_run_3hm_.size());
            if (!writer_.ensureSpace(64)) {
                st.need_output = true;
                return false;
            }
            size_t remaining = emit_literal_run_3hm_.size();
            size_t run_pos = 0;
            while (remaining > 0) {
                const size_t chunk = (remaining > max_run) ? max_run : remaining;
                huffman_tree_3hm_->encodeRunHeader(static_cast<uint16_t>(chunk), writer_);
                for (size_t i = 0; i < chunk; ++i) {
                    huffman_tree_3hm_->encodeLiteral(emit_literal_run_3hm_[run_pos + i], writer_);
                }
                run_pos += chunk;
                remaining -= chunk;
            }
            emit_literal_run_3hm_.clear();
            DPFLATE_BIN64K_LOG("EMIT_3HM", "literal_run.clear tail done");
        }
        writer_.flush();
        DPFLATE_BIN64K_LOG("EMIT_3HM", "done writer_bytes=%zu", writer_.getBytesWritten());
        // === DEBUG_BLOCK_BEGIN (可删除) ===
        static int dbg_emit_3hm_done_cnt = 0;
        if (++dbg_emit_3hm_done_cnt <= 50) {
            DEBUG_LOG("[DPFlate] handleEmitTokens 3HM done: emitted=%llu writer_bytes=%zu",
                      static_cast<unsigned long long>(emitted_tokens_),
                      writer_.getBytesWritten());
        }
        // === DEBUG_BLOCK_END ===
        DPFLATE_TRACE_SNAPSHOT(*this, "EMIT_DONE");
        st.done = true;
        return true;
    }

    while (emitted_tokens_ < total_tokens_) {
        if ((emitted_tokens_ & 0xFFFF) == 0 && algorithm::g_cancel_callback &&
            algorithm::g_cancel_callback()) {
            throw std::runtime_error("cancelled");
        }
        if (!writer_.ensureSpace(48)) {
            st.need_output = true;
            return false;
        }
        const uint64_t idx = total_tokens_ - 1 - emitted_tokens_;
        uint16_t raw_len = 0;
        uint16_t raw_off = 0;
        temp_tokens_b_.readBySeqIndex(idx, raw_len, raw_off);
        // === DEBUG_BLOCK_BEGIN (可删除) ===
        static int dbg_emit_flate_token_cnt = 0;
        if (++dbg_emit_flate_token_cnt <= 50) {
            DEBUG_LOG("[DPFlate] handleEmitTokens FLATE token: idx=%llu len=%u off=%u writer_bytes=%zu",
                      static_cast<unsigned long long>(idx), raw_len, raw_off,
                      writer_.getBytesWritten());
        }
        // === DEBUG_BLOCK_END ===
        Token token;
        if (raw_len == 0) {
            token.is_literal = true;
            token.code = raw_off;
        } else {
            token.is_literal = false;
            getLengthCode(raw_len, token.code, token.length_extra_bits, token.length_extra_val);
            getDistCode(raw_off, token.dist_code, token.dist_extra_bits, token.dist_extra_val);
        }
        writeHuffmanCode(writer_, dictionary_[token.code]);
        if (!token.is_literal) {
            for (int i = 0; i < token.length_extra_bits; i++) {
                writer_.writeBit((token.length_extra_val >> i) & 1);
            }
            writeHuffmanCode(writer_, dist_dictionary_[token.dist_code]);
            for (int i = 0; i < token.dist_extra_bits; i++) {
                writer_.writeBit((token.dist_extra_val >> i) & 1);
            }
        }
        emitted_tokens_++;
    }
    if (!writer_.ensureSpace(16)) {
        st.need_output = true;
        return false;
    }
    writeHuffmanCode(writer_, dictionary_[256]);
    writer_.flush();
    // === DEBUG_BLOCK_BEGIN (可删除) ===
    static int dbg_emit_flate_done_cnt = 0;
    if (++dbg_emit_flate_done_cnt <= 50) {
        DEBUG_LOG("[DPFlate] handleEmitTokens FLATE done: emitted=%llu writer_bytes=%zu",
                  static_cast<unsigned long long>(emitted_tokens_),
                  writer_.getBytesWritten());
    }
    // === DEBUG_BLOCK_END ===
    DPFLATE_TRACE_SNAPSHOT(*this, "EMIT_DONE");
    st.done = true;
    return true;
}

}  // namespace compressor::algorithm

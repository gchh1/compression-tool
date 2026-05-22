#include "Inflate3HM.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "BitReader.hpp"
#include "DebugLog.hpp"
#include "HuffmanTree3HM.hpp"

namespace compressor::algorithm {

Inflate3HM::Inflate3HM() { reset(); }

auto Inflate3HM::appendDecodedByte(uint8_t b) -> void {
    output_buf_.push_back(b);
    window_[static_cast<size_t>(out_abs_ % kWindowSize)] = b;
    ++out_abs_;
}

auto Inflate3HM::reset(void) -> void {
    output_buf_.clear();
    window_.assign(kWindowSize, 0);
    out_abs_ = 0;
    output_flush_pos_ = 0;
    huffman_tree_3hm_.reset();
    decode_state_ = DecodeState::READ_HEADER;
    pending_offset_ = 0;
    pending_length_ = 0;
    pending_literal_count_ = 0;
    literal_run_pos_ = 0;
}

auto Inflate3HM::handle(AlgorithmStatus& status, bool is_last_chunk) -> void {
    static int handle_call = 0;
    ++handle_call;
    int inner_iter = 0;
    while (true) {
        ++inner_iter;
        if (inner_iter > 100000000) {
            DEBUG_LOG("[Inflate3HM] SAFETY BREAK: inner_iter=%d out_abs=%llu state=%d",
                      inner_iter, (unsigned long long)out_abs_, (int)decode_state_);
            status.done = true;
            return;
        }

        if (decode_state_ == DecodeState::FLUSH_TO_WRITER) {
            // === DEBUG_BLOCK_BEGIN (可删除) ===
            static int dbg_flush_cnt = 0;
            if (++dbg_flush_cnt <= 50) {
                DEBUG_LOG("[Inflate3HM] FLUSH: output_buf=%zu flush_pos=%zu out_abs=%llu",
                          output_buf_.size(), output_flush_pos_,
                          static_cast<unsigned long long>(out_abs_));
            }
            // === DEBUG_BLOCK_END ===
            while (output_flush_pos_ < output_buf_.size()) {
                if (!writer_.ensureSpace(8)) {
                    status.need_output = true;
                    return;
                }
                writer_.writeBits(output_buf_[output_flush_pos_], 8);
                ++output_flush_pos_;
            }
            output_buf_.clear();
            output_flush_pos_ = 0;
            decode_state_ = DecodeState::DECODE_OFFSET;
            continue;
        }

        if (decode_state_ == DecodeState::READ_HEADER) {
            huffman_tree_3hm_ = std::make_unique<HuffmanTree3HM>();
            try {
                huffman_tree_3hm_->deserialize(reader_);
                DEBUG_LOG("[Inflate3HM] READ_HEADER: tree deserialized, offset_bits=%zu length_bits=%zu offset_chunk=%zu length_chunk=%zu",
                          huffman_tree_3hm_->getOffsetBits(), huffman_tree_3hm_->getLengthBits(),
                          huffman_tree_3hm_->getOffsetChunkBits(), huffman_tree_3hm_->getLengthChunkBits());
            } catch (...) {
                DEBUG_LOG("[Inflate3HM] READ_HEADER: deserialize failed, is_last_chunk=%d", is_last_chunk);
                if (is_last_chunk) {
                    status.done = true;
                    return;
                }
                status.need_input = true;
                return;
            }
            decode_state_ = DecodeState::DECODE_OFFSET;
            continue;
        }

        if (decode_state_ == DecodeState::DECODE_OFFSET) {
            if (!reader_.ensureBits(1)) {
                if (is_last_chunk) {
                    if (!output_buf_.empty()) {
                        output_flush_pos_ = 0;
                        decode_state_ = DecodeState::FLUSH_TO_WRITER;
                        continue;
                    }
                    DEBUG_LOG("[Inflate3HM] DECODE_OFFSET: done, out_abs=%llu", (unsigned long long)out_abs_);
                    status.done = true;
                    return;
                }
                status.need_input = true;
                return;
            }
            pending_offset_ = huffman_tree_3hm_->decodeOffset(reader_);

            // === DEBUG_BLOCK_BEGIN (可删除) ===
            static int dbg_offset_cnt = 0;
            if (++dbg_offset_cnt <= 50) {
                DEBUG_LOG("[Inflate3HM] DECODE_OFFSET: offset=%u out_abs=%llu",
                          pending_offset_, static_cast<unsigned long long>(out_abs_));
            }
            // === DEBUG_BLOCK_END ===

            if (pending_offset_ == 0) {
                decode_state_ = DecodeState::DECODE_RUN_LEN;
            } else {
                decode_state_ = DecodeState::DECODE_MATCH_LEN;
            }
            continue;
        }

        if (decode_state_ == DecodeState::DECODE_RUN_LEN) {
            if (!reader_.ensureBits(1)) {
                if (is_last_chunk) {
                    if (!output_buf_.empty()) {
                        output_flush_pos_ = 0;
                        decode_state_ = DecodeState::FLUSH_TO_WRITER;
                        continue;
                    }
                    status.done = true;
                    return;
                }
                status.need_input = true;
                return;
            }
            pending_literal_count_ = huffman_tree_3hm_->decodeRunLength(reader_);

            // === DEBUG_BLOCK_BEGIN (可删除) ===
            static int dbg_run_len_cnt = 0;
            if (++dbg_run_len_cnt <= 50) {
                DEBUG_LOG("[Inflate3HM] DECODE_RUN_LEN: run_len=%zu out_abs=%llu",
                          pending_literal_count_,
                          static_cast<unsigned long long>(out_abs_));
            }
            // === DEBUG_BLOCK_END ===

            literal_run_pos_ = 0;
            if (pending_literal_count_ == 0) {
                decode_state_ = DecodeState::DECODE_OFFSET;
            } else {
                decode_state_ = DecodeState::DECODE_LITERALS;
            }
            continue;
        }

        if (decode_state_ == DecodeState::DECODE_LITERALS) {
            while (literal_run_pos_ < pending_literal_count_) {
                if (!reader_.ensureBits(1)) {
                    if (is_last_chunk) {
                        if (!output_buf_.empty()) {
                            output_flush_pos_ = 0;
                            decode_state_ = DecodeState::FLUSH_TO_WRITER;
                            break;
                        }
                        status.done = true;
                        return;
                    }
                    status.need_input = true;
                    return;
                }
                uint8_t lit = huffman_tree_3hm_->decodeLiteral(reader_);
                appendDecodedByte(lit);
                ++literal_run_pos_;
            }
            decode_state_ = DecodeState::DECODE_OFFSET;
            continue;
        }

        if (decode_state_ == DecodeState::DECODE_MATCH_LEN) {
            if (!reader_.ensureBits(1)) {
                if (is_last_chunk) {
                    if (!output_buf_.empty()) {
                        output_flush_pos_ = 0;
                        decode_state_ = DecodeState::FLUSH_TO_WRITER;
                        continue;
                    }
                    status.done = true;
                    return;
                }
                status.need_input = true;
                return;
            }
            pending_length_ = huffman_tree_3hm_->decodeMatchLength(reader_);

            // === DEBUG_BLOCK_BEGIN (可删除) ===
            static int dbg_match_len_cnt = 0;
            if (++dbg_match_len_cnt <= 50) {
                DEBUG_LOG("[Inflate3HM] DECODE_MATCH_LEN: offset=%u length=%u out_abs=%llu",
                          pending_offset_, pending_length_,
                          static_cast<unsigned long long>(out_abs_));
            }
            // === DEBUG_BLOCK_END ===

            decode_state_ = DecodeState::COPY_MATCH;
            continue;
        }

        if (decode_state_ == DecodeState::COPY_MATCH) {
            if (pending_length_ == 0) {
                decode_state_ = DecodeState::DECODE_OFFSET;
                continue;
            }
            uint16_t offset = pending_offset_;
            uint16_t length = pending_length_;
            // === DEBUG_BLOCK_BEGIN (可删除) ===
            static int dbg_copy_cnt = 0;
            if (++dbg_copy_cnt <= 50) {
                DEBUG_LOG("[Inflate3HM] COPY_MATCH: offset=%u length=%u out_abs=%llu",
                          offset, length, static_cast<unsigned long long>(out_abs_));
            }
            if (offset == 0 || offset > out_abs_) {
                DEBUG_LOG("[Inflate3HM] COPY_MATCH invalid: offset=%u out_abs=%llu length=%u",
                          offset, static_cast<unsigned long long>(out_abs_), length);
                status.done = true;
                return;
            }
            // === DEBUG_BLOCK_END ===
            {
                size_t start_abs = out_abs_;
                for (uint16_t i = 0; i < length; ++i) {
                    size_t src_pos = static_cast<size_t>((start_abs - offset + i) % kWindowSize);
                    uint8_t b = window_[src_pos];
                    appendDecodedByte(b);
                }
            }
            pending_length_ = 0;
            decode_state_ = DecodeState::DECODE_OFFSET;
            continue;
        }
    }
}

}  // namespace compressor::algorithm
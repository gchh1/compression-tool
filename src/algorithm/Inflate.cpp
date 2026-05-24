#include "Inflate.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "BitReader.hpp"
#include "DebugLog.hpp"
#include "HuffmanTree.hpp"

namespace compressor::algorithm {

Inflate::Inflate() { reset(); }

auto Inflate::appendDecodedByte(uint8_t b) -> void {
    output_buf_.push_back(b);
    window_[static_cast<size_t>(out_abs_ % kWindowSize)] = b;
    ++out_abs_;
}

auto Inflate::reset(void) -> void {
    output_buf_.clear();
    window_.assign(kWindowSize, 0);
    out_abs_ = 0;
    destroyTree(lit_root_);
    destroyTree(dist_root_);
    lit_root_ = nullptr;
    dist_root_ = nullptr;
    lit_cursor_ = nullptr;
    dist_cursor_ = nullptr;
    decode_state_ = DecodeState::READ_TREES;
    output_flush_pos_ = 0;
    done_after_flush_ = false;
    pending_length_ = 0;
    pending_dist_ = 0;
    stored_bytes_remaining_ = 0;
}

void Inflate::destroyTree(node* n) {
    if (!n) return;
    destroyTree(n->left);
    destroyTree(n->right);
    delete n;
}

auto Inflate::readHuffmanTree(node*& root, size_t symbol_bits) -> bool {
    if (!reader_.ensureBits(1)) return false;
    bool is_leaf = (reader_.readBit() == 1);

    if (is_leaf) {
        if (!reader_.ensureBits(static_cast<uint8_t>(symbol_bits))) return false;
        uint16_t sym = static_cast<uint16_t>(reader_.readBits(static_cast<uint8_t>(symbol_bits)));
        root = new node(sym, 0);
    } else {
        root = new node(static_cast<uint16_t>(0), static_cast<uint32_t>(0));
        if (!readHuffmanTree(root->left, symbol_bits)) return false;
        if (!readHuffmanTree(root->right, symbol_bits)) return false;
    }
    return true;
}

void Inflate::decodeLengthCode(uint16_t symbol, uint16_t& length, uint8_t& extra_bits) {
    if (symbol < 257 || symbol > 285) {
        length = 0;
        extra_bits = 0;
        return;
    }
    size_t idx = symbol - 257;
    length = static_cast<uint16_t>(LENGTH_BASES[idx]);
    extra_bits = LENGTH_EXTRA[idx];
}

void Inflate::decodeDistCode(uint16_t symbol, uint16_t& dist, uint8_t& extra_bits) {
    if (symbol >= DISTANCE_DICTIONARY_SIZE) {
        dist = 0;
        extra_bits = 0;
        return;
    }
    dist = static_cast<uint16_t>(DIST_BASES[symbol]);
    extra_bits = DIST_EXTRA[symbol];
}

auto Inflate::handle(AlgorithmStatus& status, bool is_last_chunk) -> void {
    static int handle_call = 0;
    ++handle_call;
    int inner_iter = 0;
    while (true) {
        ++inner_iter;
        if (inner_iter > 100000000) {
            DEBUG_LOG("[Inflate] SAFETY BREAK: inner_iter=%d out_abs=%llu state=%d",
                      inner_iter, (unsigned long long)out_abs_, (int)decode_state_);
            status.done = true;
            return;
        }

        if (decode_state_ == DecodeState::READ_BLOCK_HEADER) {
            if (!reader_.ensureBits(3)) {
                status.done = true;
                status.need_input = true;
                return;
            }
            bool bfinal = reader_.readBit();
            uint8_t btype = reader_.readBits(2);
            DEBUG_LOG("[Inflate] READ_BLOCK_HEADER: bfinal=%d btype=%d", bfinal, btype);

            if (btype == 0) {
                reader_.alignToByte();
                if (!reader_.ensureBits(32)) {
                    status.need_input = true;
                    return;
                }
                uint16_t len = reader_.readBits(16);
                uint16_t nlen = reader_.readBits(16);
                (void)nlen;
                stored_bytes_remaining_ = len;
                decode_state_ = DecodeState::STORED_COPY;
            } else if (btype == 1 || btype == 2) {
                decode_state_ = DecodeState::READ_TREES;
            } else {
                status.done = true;
                return;
            }
            continue;
        }

        if (decode_state_ == DecodeState::FLUSH_TO_WRITER) {
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
            if (done_after_flush_) {
                done_after_flush_ = false;
                status.done = true;
                return;
            }
            decode_state_ = post_flush_state_;
            continue;
        }

        if (decode_state_ == DecodeState::READ_TREES) {
            destroyTree(lit_root_);
            destroyTree(dist_root_);
            lit_root_ = nullptr;
            dist_root_ = nullptr;

            if (!readHuffmanTree(lit_root_, DEFLATE_SYMBOL_BITS)) {
                if (is_last_chunk && reader_.getRemainingBits() == 0) {
                    status.done = true;
                    return;
                }
                status.need_input = true;
                return;
            }

            if (!readHuffmanTree(dist_root_, DISTANCE_SYMBOL_BITS)) {
                if (is_last_chunk && reader_.getRemainingBits() == 0) {
                    status.done = true;
                    return;
                }
                status.need_input = true;
                return;
            }

            lit_cursor_ = lit_root_;
            dist_cursor_ = dist_root_;
            decode_state_ = DecodeState::DECODE_TOKENS;
            DEBUG_LOG("[Inflate] READ_TREES done: out_abs=%llu", (unsigned long long)out_abs_);
            continue;
        }

        if (decode_state_ == DecodeState::STORED_COPY) {
            while (stored_bytes_remaining_ > 0) {
                if (!reader_.ensureBits(8)) {
                    status.need_input = true;
                    return;
                }
                appendDecodedByte(static_cast<uint8_t>(reader_.readBits(8)));
                stored_bytes_remaining_--;
            }
            output_flush_pos_ = 0;
            post_flush_state_ = DecodeState::READ_BLOCK_HEADER;
            decode_state_ = DecodeState::FLUSH_TO_WRITER;
            continue;
        }

        if (decode_state_ == DecodeState::DECODE_TOKENS) {
            if (!reader_.ensureBits(1)) {
                status.need_input = true;
                return;
            }

            bool bit = (reader_.readBit() == 1);
            lit_cursor_ = bit ? lit_cursor_->right : lit_cursor_->left;

            if (!lit_cursor_->isLeaf()) continue;

            uint16_t symbol = lit_cursor_->symbol;
            lit_cursor_ = lit_root_;

            DEBUG_LOG("[Inflate] DECODE_TOKENS: symbol=%u out_abs=%llu",
                      symbol, (unsigned long long)out_abs_);

            if (symbol < 256) {
                appendDecodedByte(static_cast<uint8_t>(symbol));
                if (output_buf_.size() >= 32768) {
                    output_flush_pos_ = 0;
                    post_flush_state_ = DecodeState::DECODE_TOKENS;
                    decode_state_ = DecodeState::FLUSH_TO_WRITER;
                }
                continue;
            }

            if (symbol == 256) {
                const bool only_padding_remains =
                    is_last_chunk && reader_.getRemainingBits() <= 7;
                if (output_buf_.empty()) {
                    if (only_padding_remains) {
                        status.done = true;
                        return;
                    }
                    decode_state_ = DecodeState::READ_TREES;
                    continue;
                }
                output_flush_pos_ = 0;
                post_flush_state_ = DecodeState::READ_TREES;
                done_after_flush_ = only_padding_remains;
                decode_state_ = DecodeState::FLUSH_TO_WRITER;
                continue;
            }

            uint16_t base_len = 0;
            uint8_t len_extra = 0;
            decodeLengthCode(symbol, base_len, len_extra);
            pending_length_ = base_len;

            if (len_extra > 0) {
                if (!reader_.ensureBits(len_extra)) {
                    status.need_input = true;
                    return;
                }
                pending_length_ += static_cast<uint16_t>(reader_.readBits(len_extra));
            }

            dist_cursor_ = dist_root_;
            while (true) {
                if (!reader_.ensureBits(1)) {
                    status.need_input = true;
                    return;
                }
                bool dbit = (reader_.readBit() == 1);
                dist_cursor_ = dbit ? dist_cursor_->right : dist_cursor_->left;
                if (dist_cursor_->isLeaf()) break;
            }

            uint16_t dist_sym = dist_cursor_->symbol;
            uint16_t base_dist = 0;
            uint8_t dist_extra_count = 0;
            decodeDistCode(dist_sym, base_dist, dist_extra_count);
            pending_dist_ = base_dist;

            if (dist_extra_count > 0) {
                if (!reader_.ensureBits(dist_extra_count)) {
                    status.need_input = true;
                    return;
                }
                pending_dist_ += static_cast<uint16_t>(reader_.readBits(dist_extra_count));
            }

            decode_state_ = DecodeState::COPY_MATCH;
            continue;
        }

        if (decode_state_ == DecodeState::COPY_MATCH) {
            DEBUG_LOG("[Inflate] COPY_MATCH: dist=%u len=%u out_abs=%llu",
                      pending_dist_, pending_length_, (unsigned long long)out_abs_);
            if (pending_dist_ == 0 || pending_dist_ > out_abs_) {
                DEBUG_LOG("[Inflate] COPY_MATCH invalid: dist=%u out_abs=%llu len=%u",
                          pending_dist_, (unsigned long long)out_abs_, pending_length_);
                status.done = true;
                return;
            }
            const uint64_t base = out_abs_;
            for (size_t i = 0; i < pending_length_; ++i) {
                const uint64_t src_abs = base - static_cast<uint64_t>(pending_dist_) + i;
                appendDecodedByte(
                    window_[static_cast<size_t>(src_abs % kWindowSize)]);
            }

            if (output_buf_.size() >= 32768) {
                output_flush_pos_ = 0;
                post_flush_state_ = DecodeState::DECODE_TOKENS;
                decode_state_ = DecodeState::FLUSH_TO_WRITER;
            } else {
                decode_state_ = DecodeState::DECODE_TOKENS;
            }
            continue;
        }
    }
}

}  // namespace compressor::algorithm
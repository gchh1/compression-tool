#include "Inflate.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>

#include "BitReader.hpp"
#include "BitWriter.hpp"
#include "HuffmanTree.hpp"

namespace compressor::algorithm {

// ---- Length lookup tables (matching Deflate::getLengthCode) ----
const uint16_t Inflate::kLengthBase[] = {
    3,   4,   5,   6,   7,   8,   9,   10,   // 257-264
    11,  13,  15,  17,                         // 265-268
    19,  23,  27,  31,                         // 269-272
    35,  43,  51,  59,                         // 273-276
    67,  83,  99,  115,                        // 277-280
    131, 163, 195, 227,                        // 281-284
    258                                        // 285
};
const uint8_t Inflate::kLengthExtraBits[] = {
    0, 0, 0, 0, 0, 0, 0, 0,  // 257-264
    1, 1, 1, 1,              // 265-268
    2, 2, 2, 2,              // 269-272
    3, 3, 3, 3,              // 273-276
    4, 4, 4, 4,              // 277-280
    5, 5, 5, 5,              // 281-284
    0                         // 285
};

// ---- Distance lookup tables (matching Deflate::getDistCode) ----
const uint16_t Inflate::kDistanceBase[] = {
    1,    2,    3,    4,          // 0-3
    5,    7,                       // 4-5
    9,    13,                      // 6-7
    17,   25,                      // 8-9
    33,   49,                      // 10-11
    65,   97,                      // 12-13
    129,  193,                     // 14-15
    257,  385,                     // 16-17
    513,  769,                     // 18-19
    1025, 1537,                    // 20-21
    2049, 3073,                    // 22-23
    4097, 6145,                    // 24-25
    8193, 12289,                   // 26-27
    16385, 24577                   // 28-29
};
const uint8_t Inflate::kDistanceExtraBits[] = {
    0,  0,  0,  0,   // 0-3
    1,  1,            // 4-5
    2,  2,            // 6-7
    3,  3,            // 8-9
    4,  4,            // 10-11
    5,  5,            // 12-13
    6,  6,            // 14-15
    7,  7,            // 16-17
    8,  8,            // 18-19
    9,  9,            // 20-21
    10, 10,           // 22-23
    11, 11,           // 24-25
    12, 12,           // 26-27
    13, 13            // 28-29
};

Inflate::Inflate() { reset(); }

auto Inflate::reset(void) -> void {
    window_.assign(DICTIONARY_SIZE, 0);
    decode_pos_ = 0;
    is_last_block_ = false;
    inflate_state_ = InflateState::READ_BLOCK_HEADER;
    ll_tree_.reset();
    dist_tree_.reset();
    ll_cursor_ = nullptr;
    dist_cursor_ = nullptr;
    copy_length_ = 0;
    copy_distance_ = 0;
    pending_byte_ = 0;
    has_pending_ = false;
    pending_extra_ = 0;
    has_pending_extra_ = false;
}

auto Inflate::handle(AlgorithmStatus& status, bool /*is_last_chunk*/) -> void {
    while (true) {
        // ---- Phase: COPY (only when between tokens, not mid-decode) ----
        while (copy_length_ > 0 &&
               inflate_state_ == InflateState::DECODE_TOKEN) {
            // Flush pending literal before match copy
            if (has_pending_) {
                if (writer_.getRemainSize() == 0) {
                    status.need_output = true;
                    return;
                }
                writer_.writeBytes(&pending_byte_, 1);
                window_[decode_pos_ & DICT_MASK] = pending_byte_;
                decode_pos_++;
                has_pending_ = false;
                continue;  // retry copy after flush
            }

            if (writer_.getRemainSize() == 0) {
                status.need_output = true;
                return;
            }
            uint8_t byte =
                window_[(decode_pos_ - copy_distance_) & DICT_MASK];
            writer_.writeBytes(&byte, 1);
            window_[decode_pos_ & DICT_MASK] = byte;
            decode_pos_++;
            copy_length_--;
        }

        // Flush any pending literal
        if (has_pending_) {
            if (writer_.getRemainSize() == 0) {
                status.need_output = true;
                return;
            }
            writer_.writeBytes(&pending_byte_, 1);
            window_[decode_pos_ & DICT_MASK] = pending_byte_;
            decode_pos_++;
            has_pending_ = false;
        }

        // ---- Input check ----
        if (reader_.getRemainingBits() == 0) {
            status.need_input = true;
            return;
        }

        // ---- Resume pending extra bits if interrupted ----
        if (has_pending_extra_) {
            uint8_t extra = pending_extra_;
            has_pending_extra_ = false;
            if (reader_.getRemainingBits() < extra) {
                pending_extra_ = extra;
                has_pending_extra_ = true;
                status.need_input = true;
                return;
            }
            uint16_t ev = static_cast<uint16_t>(reader_.readBits(extra));
            if (inflate_state_ == InflateState::DECODE_TOKEN) {
                copy_length_ += ev;
                inflate_state_ = InflateState::DECODE_DISTANCE;
            } else {
                copy_distance_ += ev;
                inflate_state_ = InflateState::DECODE_TOKEN;
            }
            continue;
        }

        // ---- State machine ----
        switch (inflate_state_) {
            case InflateState::READ_BLOCK_HEADER: {
                uint64_t bfinal = reader_.readBit();
                uint64_t btype = reader_.readBits(2);
                is_last_block_ = (bfinal == 1);
                inflate_state_ = InflateState::READ_LL_TREE;
                break;
            }

            case InflateState::READ_LL_TREE: {
                if (reader_.getRemainSize() < 1) {
                    status.need_input = true;
                    return;
                }
                ll_tree_ = std::make_unique<HuffmanTree>(reader_, 9, 286);
                ll_cursor_ = ll_tree_->getRoot();
                if (!ll_cursor_) {
                    status.need_input = true;
                    return;
                }
                inflate_state_ = InflateState::READ_D_TREE;
                break;
            }

            case InflateState::READ_D_TREE: {
                if (reader_.getRemainSize() < 1) {
                    status.need_input = true;
                    return;
                }
                dist_tree_ =
                    std::make_unique<HuffmanTree>(reader_, 5, 30);
                dist_cursor_ = dist_tree_->getRoot();
                if (!dist_cursor_) {
                    status.need_input = true;
                    return;
                }
                inflate_state_ = InflateState::DECODE_TOKEN;
                break;
            }

            case InflateState::DECODE_TOKEN: {
                if (reader_.getRemainingBits() == 0) {
                    status.need_input = true;
                    return;
                }
                uint64_t bit = reader_.readBit();
                if (!ll_cursor_) {
                    status.need_input = true;
                    return;
                }
                ll_cursor_ = (bit == 0) ? ll_cursor_->left : ll_cursor_->right;
                if (!ll_cursor_) {
                    status.need_input = true;
                    return;
                }

                if (!ll_cursor_->isLeaf()) break;

                uint16_t symbol = ll_cursor_->symbol;
                ll_cursor_ = ll_tree_->getRoot();

                if (symbol < 256) {
                    if (writer_.getRemainSize() == 0) {
                        pending_byte_ = static_cast<uint8_t>(symbol);
                        has_pending_ = true;
                        status.need_output = true;
                        return;
                    }
                    uint8_t b = static_cast<uint8_t>(symbol);
                    writer_.writeBytes(&b, 1);
                    window_[decode_pos_ & DICT_MASK] = b;
                    decode_pos_++;
                } else if (symbol == 256) {
                    if (is_last_block_) {
                        status.done = true;
                        return;
                    }
                    inflate_state_ = InflateState::READ_BLOCK_HEADER;
                    if (writer_.getRemainSize() == 0) {
                        status.need_output = true;
                        return;
                    }
                } else {
                    size_t idx = symbol - 257;
                    copy_length_ = kLengthBase[idx];
                    uint8_t extra = kLengthExtraBits[idx];
                    if (extra > 0) {
                        if (reader_.getRemainingBits() < extra) {
                            pending_extra_ = extra;
                            has_pending_extra_ = true;
                            status.need_input = true;
                            return;
                        }
                        uint16_t ev = static_cast<uint16_t>(
                            reader_.readBits(extra));
                        copy_length_ += ev;
                    }
                    inflate_state_ = InflateState::DECODE_DISTANCE;
                }
                break;
            }

            case InflateState::DECODE_DISTANCE: {
                if (reader_.getRemainingBits() == 0) {
                    status.need_input = true;
                    return;
                }
                uint64_t bit = reader_.readBit();
                if (!dist_cursor_) {
                    status.need_input = true;
                    return;
                }
                dist_cursor_ =
                    (bit == 0) ? dist_cursor_->left : dist_cursor_->right;
                if (!dist_cursor_) {
                    status.need_input = true;
                    return;
                }

                if (!dist_cursor_->isLeaf()) break;

                uint16_t dist_code = dist_cursor_->symbol;
                dist_cursor_ = dist_tree_->getRoot();

                copy_distance_ = kDistanceBase[dist_code];
                uint8_t extra = kDistanceExtraBits[dist_code];
                if (extra > 0) {
                    if (reader_.getRemainingBits() < extra) {
                        pending_extra_ = extra;
                        has_pending_extra_ = true;
                        status.need_input = true;
                        return;
                    }
                    uint16_t ev = static_cast<uint16_t>(
                        reader_.readBits(extra));
                    copy_distance_ += ev;
                }

                inflate_state_ = InflateState::DECODE_TOKEN;
                break;
            }
        }
    }
}

}  // namespace compressor::algorithm

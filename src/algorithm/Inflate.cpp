#include "Inflate.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "BitReader.hpp"
#include "HuffmanTree.hpp"

namespace compressor::algorithm {

Inflate::Inflate() { reset(); }

auto Inflate::reset(void) -> void {
    output_buf_.clear();
    destroyTree(lit_root_);
    destroyTree(dist_root_);
    lit_root_ = nullptr;
    dist_root_ = nullptr;
    lit_cursor_ = nullptr;
    dist_cursor_ = nullptr;
    decode_state_ = DecodeState::READ_TREES;
    pending_length_ = 0;
    pending_dist_ = 0;
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
    while (true) {
        if (decode_state_ == DecodeState::READ_TREES) {
            destroyTree(lit_root_);
            destroyTree(dist_root_);
            lit_root_ = nullptr;
            dist_root_ = nullptr;

            if (!readHuffmanTree(lit_root_, DEFLATE_SYMBOL_BITS)) {
                status.need_input = true;
                return;
            }

            if (!readHuffmanTree(dist_root_, DISTANCE_SYMBOL_BITS)) {
                status.need_input = true;
                return;
            }

            lit_cursor_ = lit_root_;
            dist_cursor_ = dist_root_;
            decode_state_ = DecodeState::DECODE_TOKENS;
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

            if (symbol < 256) {
                output_buf_.push_back(static_cast<uint8_t>(symbol));
                continue;
            }

            if (symbol == 256) {
                for (size_t i = 0; i < output_buf_.size(); i++) {
                    if (!writer_.ensureSpace(1)) {
                        status.need_output = true;
                        return;
                    }
                    writer_.writeBits(output_buf_[i], 8);
                }
                output_buf_.clear();
                status.done = true;
                return;
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
            if (pending_dist_ > output_buf_.size()) {
                status.done = false;
                return;
            }
            size_t src_start = output_buf_.size() - pending_dist_;
            for (size_t i = 0; i < pending_length_; i++) {
                output_buf_.push_back(output_buf_[src_start + i]);
            }

            decode_state_ = DecodeState::DECODE_TOKENS;
            continue;
        }
    }
}

}  // namespace compressor::algorithm


#include "Inflate.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "BitReader.hpp"
#include "Deflate.hpp"
#include "HuffmanTree.hpp"

namespace compressor::algorithm {

Inflate::Inflate() { reset(); }

auto Inflate::reset(void) -> void {
    window_.resize(DICTIONARY_SIZE, 0);
    is_last_block_ = false;
    decode_state_ = DecodeState::READ_BLOCK_HEADER;
    huffman_tree_ = nullptr;
    cursor_ = nullptr;

    decode_buffer_ = 0;
    decode_buffer_idx_ = 0;

    copy_length_ = 0;
    copy_distance_ = 0;
}

auto Inflate::handle(AlgorithmStatus& status, bool is_last_chunk) -> void {
    size_t bytes_written = 0;

    while (bytes_written < write.size()) {
        if (copy_length_ > 0) {
            uint8_t ch =
                window_[(decode_pos_ = copy_distance_) & (DICTIONARY_SIZE - 1)];

            write[bytes_written++] = ch;

            window_[decode_pos_ & (DICTIONARY_SIZE - 1)] = ch;

            decode_pos_++;
            copy_length_--;
            continue;
        }

        // ===================================
        // State #1:
        // ===================================
        if (decode_state_ == DecodeState::READ_BLOCK_HEADER) {
            if (reader.isEOF()) break;

            int bit = reader.readBit();
            if (reader.isEOF()) break;

            is_last_block_ = (bit == 1);
            decode_state_ = DecodeState::READ_TREE;
        }

        // ===================================
        // State #2:
        // ===================================
        if (decode_state_ == DecodeState::READ_TREE) {
            huffman_tree_ = std::make_unique<HuffmanTree>(reader);

            if (reader.isEOF()) break;

            cursor_ = huffman_tree_->getRoot();
            decode_state_ = DecodeState::DECODE_TOKENS;
        }

        // ===================================
        // State #3:
        // ===================================
        if (decode_state_ == DecodeState::DECODE_TOKENS) {
            uint8_t bit = reader.readBit();

            if (reader.isEOF()) break;

            cursor_ = (bit == 0) ? cursor_->left : cursor_->right;

            if (cursor_ && cursor_->isLeaf()) {
                uint16_t symbol = cursor_->symbol;

                if (symbol < 256) {
                    uint8_t ch = static_cast<uint8_t>(symbol);
                    write[bytes_written++] = ch;
                    window_[decode_pos_ & (DICTIONARY_SIZE - 1)] = ch;
                    decode_pos_++;
                } else if (symbol == 256) {
                    if (is_last_block_) {
                        reset();
                        return bytes_written;
                    } else {
                        decode_state_ = DecodeState::READ_BLOCK_HEADER;
                    }
                } else {
                    copy_length_ = symbol - 257 + 3;

                    copy_distance_ = static_cast<uint16_t>(reader.readBits(15));

                    if (reader.isEOF()) break;
                }

                cursor_ = huffman_tree_->getRoot();
            }
        }
    }

    decode_buffer_ = reader.getBuffer();
    decode_buffer_idx_ = reader.getBufferIdx();

    return bytes_written;
}
}  // namespace compressor::algorithm
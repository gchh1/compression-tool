// Include lib here
#include "Huffman.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "BitReader.hpp"
#include "BitWriter.hpp"
#include "HuffmanTree.hpp"

namespace compressor::algorithm {

auto Huffman::reset(void) -> void {
    encode_buffer_ = 0;
    encode_buffer_idx_ = 0;
    decode_buffer_ = 0;
    decode_buffer_idx_ = 0;
    decode_state_ = DecodeState::READ_SIZE;
    current_decode_size_ = 0;

    huffman_tree_.reset();
    current_cursor_ = nullptr;
}

/**
 * @brief For each `chunk`, we compress it to [original size][huffman tree for
 * this chunk][compressed data] 4bytes
 *
 * @param input
 * @return std::vector<uint8_t>
 */
auto Huffman::compress(std::span<const uint8_t> read, std::span<uint8_t> write,
                       bool is_last) -> size_t {
    if (read.empty() && !is_last) {
        return 0;
    }

    /* 0. Instantial BitReader and BitWriter*/
    utils::BitWriter writer(write, encode_buffer_, encode_buffer_idx_);

    if (!read.empty()) {
        /* 1. Push the size of input data into the result */
        // The ahead 4 bytes of result is the size of original data
        writer.writeBits(static_cast<uint32_t>(read.size()), 32);

        /* 2. Build and get the huffman tree */
        HuffmanTree huffman_tree(read);
        auto dictionary = huffman_tree.buildDictionary();

        /* 3. Serialize the Huffman tree */
        huffman_tree.serializeTree(writer);

        /* 4. Compress the input */
        for (const uint8_t &item : read) {
            // Get the Huffman code for each byte
            const auto &huffman_code = dictionary[item];

            writer.writeBits(huffman_code.code, huffman_code.length);
        }
    }

    if (is_last) {
        writer.flush();
        encode_buffer_ = 0;
        encode_buffer_idx_ = 0;
    } else {
        encode_buffer_ = writer.getBuffer();
        encode_buffer_idx_ = writer.getBufferIdx();
    }

    return writer.getBytesWritten();
}

/**
 * @brief Since the compressed chunk is not chunk aligned, `decode_staete_` is
 * used.
 *
 * @param input
 * @return std::vector<uint8_t>
 */
auto Huffman::decompress(std::span<const uint8_t> read,
                         std::span<uint8_t> write) -> size_t {
    /* 0. Instantialize `BitReader` */
    utils::BitReader reader(read, decode_buffer_, decode_buffer_idx_);
    size_t bytes_written = 0;

    /* 1. Decode the size of original data */
    if (decode_state_ == DecodeState::READ_SIZE) {
        uint64_t size_val = reader.readBits(32);

        if (reader.isEOF()) {
            goto SAVE_STATE;
        }

        target_size_ = static_cast<uint32_t>(size_val);
        decode_state_ = DecodeState::READ_TREE;
    }

    /* 2. Decode the Huffman Tree */
    if (decode_state_ == DecodeState::READ_TREE) {
        huffman_tree_ = std::make_unique<HuffmanTree>(reader);
        if (reader.isEOF()) {
            goto SAVE_STATE;
        }
        current_cursor_ = huffman_tree_->getRoot();
        decode_state_ = DecodeState::DECODE_DATA;
    }

    /* 3. Decode the data by the Tree */
    if (decode_state_ == DecodeState::DECODE_DATA) {
        while (current_decode_size_ < target_size_) {
            if (bytes_written >= write.size()) {
                break;
            }

            uint8_t bit = reader.readBit();

            if (reader.isEOF()) {
                break;
            }

            current_cursor_ =
                (bit == 0) ? current_cursor_->left : current_cursor_->right;

            if (current_cursor_ != nullptr && current_cursor_->isLeaf()) {
                write[bytes_written++] = current_cursor_->symbol;
                current_decode_size_++;

                current_cursor_ = huffman_tree_->getRoot();
            }
        }

        if (current_decode_size_ >= target_size_) {
            decode_state_ = DecodeState::READ_SIZE;
            huffman_tree_.reset();
            current_cursor_ = nullptr;
        }
    }

SAVE_STATE:
    decode_buffer_ = reader.getBuffer();
    decode_buffer_idx_ = reader.getBufferIdx();

    return bytes_written;
}

}  // namespace compressor::algorithm

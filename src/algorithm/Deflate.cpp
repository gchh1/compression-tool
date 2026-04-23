// Include lib here

#include "Deflate.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "BitWriter.hpp"
#include "Deflate.hpp"
#include "Huffman.hpp"
#include "HuffmanTree.hpp"

namespace compressor {
namespace algorithm {

/**
 * @brief Construct a new Deflate:: Deflate object
 *
 */
Deflate::Deflate() { reset(); }

/**
 * @brief
 *
 */
auto Deflate::reset(void) -> void {
    window_.resize(WINDOW_SIZE, 0);

    head_.assign(HASH_SIZE, NULL_PTR);
    prev_.assign(SLIDE_SIZE, NULL_PTR);

    cursor_ = 0;
    lookahead_ = 0;

    token_buffer_.clear();
    encode_buffer_ = 0;
    encode_buffer_idx_ = 0;
}

/**
 * @brief
 *
 * @param read
 * @param read_offset
 */
auto Deflate::fillWindow(std::span<const uint8_t>& read, size_t& read_offset)
    -> void {
    while (lookahead_ < WINDOW_SIZE && read_offset < read.size()) {
        if (cursor_ + lookahead_ >= WINDOW_SIZE) {
            slideWindow();
        }

        size_t space_left = WINDOW_SIZE - (cursor_ + lookahead_);
        size_t data_left = read.size() - read_offset;
        size_t copy_size = std::min(space_left, data_left);

        std::memcpy(window_.data() + cursor_ + lookahead_,
                    read.data() + read_offset, copy_size);

        lookahead_ += copy_size;
        read_offset += copy_size;
    }
}

/**
 * @brief
 *
 */
auto Deflate::slideWindow(void) -> void {
    std::memcpy(window_.data(), window_.data() + SLIDE_SIZE, SLIDE_SIZE);

    cursor_ -= SLIDE_SIZE;

    for (size_t i = 0; i < HASH_SIZE; ++i) {
        head_[i] = (head_[i] >= SLIDE_SIZE)
                       ? static_cast<uint16_t>(head_[i] - SLIDE_SIZE)
                       : NULL_PTR;
    }

    for (size_t i = 0; i < SLIDE_SIZE; ++i) {
        prev_[i] = (prev_[i] >= SLIDE_SIZE)
                       ? static_cast<uint16_t>(prev_[i] - SLIDE_SIZE)
                       : NULL_PTR;
    }
}

/**
 * @brief When `tokens` reach maximum, encode by Huffman and write.
 *
 *        Rather than use the encode function provided by `Huffman` class,
 *        we
 *
 * @param writer
 * @param is_last_block
 * @return size_t
 */
auto Deflate::flushBlock(utils::BitWriter& writer, bool is_last_block)
    -> size_t {
    // Edge detection
    if (token_buffer_.empty() && !is_last_block) return 0;

    std::vector<uint32_t> freq_map(DEFLATE_ALPHABET_SIZE, 0);
    for (const auto& token : token_buffer_) {
        if (token.is_literal) {
            freq_map[token.value]++;
        } else {
            freq_map[257 + (token.value - 3)]++;
        }
    }

    freq_map[256] = 1;

    HuffmanTree huffman_tree(freq_map);
    auto dictionary = huffman_tree.buildDictionary();

    writer.writeBit(is_last_block ? 1 : 0);

    huffman_tree.serializeTree(writer);

    for (const auto& token : token_buffer_) {
        if (token.is_literal) {
            const auto& code = dictionary[token.value];
            writer.writeBits(code.code, code.length);
        } else {
            const auto& len_code = dictionary[257 + (token.value) - 3];
            writer.writeBits(len_code.code, len_code.length);

            writer.writeBits(token.distance, 15);
        }

        const auto& eof_code = dictionary[256];
        writer.writeBits(eof_code.code, eof_code.length);

        token_buffer_.clear();

        Huffman huffman;
    }
    return writer.getBytesWritten();
}

/**
 * @brief LZSS with hash table optimization
 *
 * @param input
 * @return std::vector<Token>
 */
auto Deflate::compress(std::span<const uint8_t> read, std::span<uint8_t> write,
                       bool is_last) -> size_t {
    utils::BitWriter writer(write, encode_buffer_, encode_buffer_idx_);
    size_t read_offset = 0;

    while (read_offset < read.size() || lookahead_ > 0) {
        //
        if (lookahead_ < MIN_MATCH && read_offset < read.size()) {
            fillWindow(read, read_offset);
        }

        if (lookahead_ == 0) break;

        size_t match_length = 0;
        size_t match_distance = 0;

        if (lookahead_ >= MIN_MATCH) {
            uint16_t hash_val = getHash(cursor_);
            uint16_t match_pos = head_[hash_val];

            prev_[cursor_ & (SLIDE_SIZE - 1)] = match_pos;
            head_[hash_val] = static_cast<uint16_t>(cursor_);

            size_t chain_length = MAX_CHAIN_LENGTH;
            while (match_pos != NULL_PTR && chain_length-- > 0) {
                size_t distance = cursor_ - match_pos;

                if (distance > SLIDE_SIZE || distance == 0) break;

                size_t current_len = 0;
                size_t max_possible =
                    std::min({MAX_MATCH, lookahead_, WINDOW_SIZE - cursor_});

                while (current_len < max_possible &&
                       window_[cursor_ + current_len] ==
                           window_[match_pos + current_len]) {
                    current_len++;
                }

                if (current_len > match_length) {
                    match_length = current_len;
                    match_distance = distance;
                    if (match_length == max_possible) break;
                }

                match_pos = prev_[match_pos & (SLIDE_SIZE - 1)];
            }
        }

        if (match_length >= MIN_MATCH) {
            token_buffer_.push_back({false, static_cast<uint16_t>(match_length),
                                     static_cast<uint16_t>(match_distance)});

            for (size_t i = 1; i < match_length; ++i) {
                cursor_++;
                lookahead_--;
                if (lookahead_ >= MIN_MATCH) {
                    uint16_t hash_val = getHash(cursor_);
                    prev_[cursor_ & (SLIDE_SIZE - 1)] = head_[hash_val];
                    head_[hash_val] = static_cast<uint16_t>(cursor_);
                }
            }

            cursor_++;
            lookahead_--;
        } else {
            token_buffer_.push_back({true, window_[cursor_], 0});
            cursor_++;
            lookahead_--;
        }

        if (token_buffer_.size() >= MAX_BLOCK_TOKENS) {
            flushBlock(writer, false);
        }
    }

    if (is_last) {
        flushBlock(writer, true);
        writer.flush();
        reset();
    } else {
        encode_buffer_ = writer.getBuffer();
        encode_buffer_idx_ = writer.getBufferIdx();
    }

    return writer.getBytesWritten();
}

/**
 * @brief
 *
 * @param tokens
 * @return std::vector<uint8_t>
 */
std::vector<uint8_t> Deflate::decompress(const std::vector<Token>& tokens) {
    std::vector<uint8_t> result;

    for (const auto& token : tokens) {
        // Meet (1, char)
        if (token.is_literal) {
            result.push_back(token.value);
        }
        // Meet (0, length, position)
        else {
            size_t start_idx = result.size() - token.position;
            for (size_t i = 0; i < token.value; ++i) {
                uint8_t ch = result[start_idx + i];
                result.push_back(ch);
            }
        }
    }

    return result;
}

}  // namespace algorithm

}  // namespace compressor
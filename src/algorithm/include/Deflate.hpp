#pragma once

// Include lib here
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "BitWriter.hpp"

namespace compressor::algorithm {

/** @brief Deflate token */
struct Token {
    bool is_literal;

    /** @brief If `is_literal` is true, the value is `0-255` ASCII. Otherwise,
     *         store the length of matched `word` */
    uint16_t value;

    /** @brief `Distance` between the two match items */
    uint16_t distance;
};

/** @brief  */
constexpr uint16_t NULL_PTR = 0xffff;

class Deflate {
   public:
    Deflate();

    auto compress(std::span<const uint8_t> read, std::span<uint8_t> write,
                  bool is_last) -> size_t;

    auto decompress(std::span<const uint8_t> read, std::span<uint8_t> write)
        -> size_t;

    auto reset(void) -> void;

   private:
    static constexpr size_t SLIDE_SIZE = 32768;            // 32KB slide window
    static constexpr size_t WINDOW_SIZE = 2 * SLIDE_SIZE;  // 64KB double buffer
    static constexpr size_t MIN_MATCH = 3;           // Minimum match length
    static constexpr size_t MAX_MATCH = 258;         // Maximum match length
    static constexpr size_t HASH_SIZE = 32768;       //
    static constexpr size_t MAX_CHAIN_LENGTH = 256;  // Prevent deep search

    /** @brief When `tokens` in the `token_buffer_` reach the value, encode and
     *         flush the `tokens` */
    static constexpr size_t MAX_BLOCK_TOKENS = 16384;

    // ===================================
    // Chunk state
    // ===================================
    std::vector<uint8_t> window_;
    std::vector<uint16_t> head_;
    std::vector<uint16_t> prev_;

    size_t cursor_{0};
    size_t lookahead_{0};

    // ===================================
    // Token buffer and Huffman
    // ===================================
    std::vector<Token> token_buffer_;

    uint64_t encode_buffer_{0};
    uint8_t encode_buffer_idx_{0};

    // ===================================
    // Private method
    // ===================================
    /** @brief Return the hash code */
    inline auto getHash(size_t pos) -> uint16_t {
        return ((window_[pos] << 10) ^ (window_[pos + 1] << 5) ^
                window_[pos + 2]) &
               (HASH_SIZE - 1);
    }

    auto fillWindow(std::span<const uint8_t>& read, size_t& read_offset)
        -> void;

    auto slideWindow(void) -> void;

    auto flushBlock(utils::BitWriter& writer, bool is_last_block) -> size_t;
};

}  // namespace compressor::algorithm

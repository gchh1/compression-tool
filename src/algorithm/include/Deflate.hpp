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
    bool is_literal;    // true: character; false: (length, position)
    uint16_t value;     // char or length
    uint16_t position;  // position
};

class Deflate {
   public:
    Deflate();

    auto compress(std::span<const uint8_t> read, std::span<uint8_t> write,
                  bool is_last) -> void;

    auto decompress(std::span<const uint8_t> read, std::span<uint8_t> write)
        -> void;

    auto reset(void) -> void;

   private:
    static constexpr size_t SLIDE_SIZE = 32768;            // 32KB slide window
    static constexpr size_t WINDOW_SIZE = 2 * SLIDE_SIZE;  // 64KB double buffer
    static constexpr size_t MIN_MATCH = 3;             // Minimum match length
    static constexpr size_t MAX_MATCH = 258;           // Maximum match length
    static constexpr size_t HASH_SIZE = 32768;         //
    static constexpr size_t MAX_CHAIN_LENGTH = 256;    // Prevent deep search
    static constexpr size_t MAX_BLOCK_TOKENS = 16384;  //

    // ===================================
    // Chunk state
    // ===================================
    std::vector<uint8_t> window_;
    std::vector<uint16_t> head_;
    std::vector<uint16_t> prev_;

    size_t insert_pos_{0};
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
    inline uint16_t getHash(const std::vector<uint8_t>& data, size_t pos) {
        return ((data[pos] << 10) ^ (data[pos + 1] << 5) ^ data[pos + 2]) &
               (HASH_SIZE - 1);
    }

    auto fillWindow(std::span<const uint8_t>& read, size_t& read_offset)
        -> void;

    auto slideWindow(void) -> void;

    auto flushBlock(utils::BitWriter& writer, bool is_last_block) -> size_t;
};

}  // namespace compressor::algorithm

#pragma once

// Include lib here
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "HuffmanTree.hpp"
#include "IAlgorithm.hpp"

namespace compressor::algorithm {

/** @brief Deflate token */
struct Token {
    bool is_literal;

    /** @brief If `is_literal` = true, code is from 0 to 255 (ASCII), otherwise,
     *         code is from 257 to 285 (Length code) */
    uint16_t code;

    uint8_t length_extra_bits;
    uint16_t length_extra_val;

    uint8_t dist_code;
    uint8_t dist_extra_bits;
    uint16_t dist_extra_val;
};

/** @brief  */
constexpr uint16_t NULL_PTR = 0xffff;

class Deflate : public AlgorithmBase {
   public:
    /// ``lookahead_max`` caps LZ77 match length (clamped to 258 for valid DEFLATE length codes).
    /// Pass 0 for 258 (full deflate match limit).
    Deflate(size_t slide_size = 4096, size_t min_match = 3,
            size_t max_chain_length = 256, size_t lookahead_max = 258);

    auto reset(void) -> void override;

   protected:
    auto handle(AlgorithmStatus& algorithm_status, bool is_last_chunk)
        -> void override;

   private:
    // ===================================
    // State machine
    // ===================================
    enum class DeflateState { FIND_MATCHES, BUILD_TREE, FLUSH_TOKENS };

    DeflateState deflate_state_{DeflateState::FIND_MATCHES};

    auto handleFindMatches(AlgorithmStatus& status, bool is_last_chunk) -> void;
    auto handleBuildTree(AlgorithmStatus& status, bool is_last_chunk) -> void;
    auto handleFlushTokens(AlgorithmStatus& status, bool is_last_chunk) -> void;

    using StateHandler = void (Deflate::*)(AlgorithmStatus&, bool);
    static constexpr StateHandler kStateHandlers[3] = {
        &Deflate::handleFindMatches, &Deflate::handleBuildTree,
        &Deflate::handleFlushTokens};

    // ===================================
    // Deflate parameters
    // ===================================
    size_t SLIDE_SIZE;
    size_t WINDOW_SIZE;
    size_t MIN_MATCH;
    size_t MAX_MATCH;
    size_t HASH_SIZE;
    size_t MAX_CHAIN_LENGTH;
    static constexpr size_t DISTANCE_DICTIONARY_SIZE = 30;
    static constexpr size_t DISTANCE_SYMBOL_BITS = 5;

    static constexpr size_t MAX_BLOCK_TOKENS = 16384;

    // ===================================
    // Deflate state
    // ===================================
    std::vector<uint8_t> window_;
    std::vector<uint16_t> head_;
    std::vector<uint16_t> prev_;

    size_t cursor_{0};
    size_t lookahead_{0};

    std::vector<Token> token_buffer_;
    size_t token_flush_idx_{0};

    std::unique_ptr<HuffmanTree> huffman_tree_;
    std::unique_ptr<HuffmanTree> dist_tree_;

    std::vector<HuffmanCode> dictionary_;
    std::vector<HuffmanCode> dist_dictionary_;
    // ===================================
    // Private methods
    // ===================================
    /** @brief Return the hash code */
    inline auto getHash(size_t pos) -> uint16_t {
        return ((window_[pos] << 10) ^ (window_[pos + 1] << 5) ^
                window_[pos + 2]) &
               (HASH_SIZE - 1);
    }

    auto fillWindow(void) -> void;

    auto slideWindow(void) -> void;

    void getLengthCode(size_t length, uint16_t& code, uint8_t& extra_bits,
                       uint16_t& extra_val);

    void getDistCode(size_t dist, uint8_t& code, uint8_t& extra_bits,
                     uint16_t& extra_val);
};

}  // namespace compressor::algorithm

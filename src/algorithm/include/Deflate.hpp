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
    Deflate();

    auto reset(void) -> void override;

    auto getBlockProfile() -> std::optional<BlockProfile> override;

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
    static constexpr size_t SLIDE_SIZE = 32768;            // 32KB slide window
    static constexpr size_t WINDOW_SIZE = 2 * SLIDE_SIZE;  // 64KB double buffer
    static constexpr size_t MIN_MATCH = 3;           // Minimum match length
    static constexpr size_t MAX_MATCH = 258;         // Maximum match length
    static constexpr size_t HASH_SIZE = 32768;       //
    static constexpr size_t MAX_CHAIN_LENGTH = 256;  // Prevent deep search
    static constexpr size_t DISTANCE_DICTIONARY_SIZE = 30;
    static constexpr size_t DISTANCE_SYMBOL_BITS = 5;

    /** @brief When `tokens` in the `token_buffer_` reach the value, encode and
     *         flush the `tokens` */
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

    bool bfinal_{false};

    std::vector<BlockInfo> block_profile_;

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

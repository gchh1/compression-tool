#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "HuffmanTree.hpp"
#include "IAlgorithm.hpp"

namespace compressor::algorithm {

constexpr size_t BROTLI_LIT_ALPHABET = 257;   // 0-255 literal, 256 = EOF
constexpr uint8_t BROTLI_LIT_SYMBOL_BITS = 9;
constexpr size_t BROTLI_LEN_ALPHABET = 29;    // length codes 0-28
constexpr uint8_t BROTLI_LEN_SYMBOL_BITS = 5;
constexpr size_t BROTLI_DIST_ALPHABET = 30;   // distance codes 0-29
constexpr uint8_t BROTLI_DIST_SYMBOL_BITS = 5;

struct BrotliToken {
    bool is_literal;
    uint8_t literal;

    uint8_t length_code;          // 0-28 -> maps to actual length 3-258
    uint8_t length_extra_bits;
    uint16_t length_extra_val;

    uint8_t dist_code;            // 0-29 -> maps to actual distance 1-32768+
    uint8_t dist_extra_bits;
    uint16_t dist_extra_val;
};

class BrotliCompress : public AlgorithmBase {
public:
    BrotliCompress(size_t window_size = 65536, size_t min_match = 3,
                   size_t max_chain_length = 256);

    auto reset(void) -> void override;

protected:
    auto handle(AlgorithmStatus& algorithm_status, bool is_last_chunk)
        -> void override;

private:
    enum class BrotliState { FIND_MATCHES, BUILD_TREE, FLUSH_TOKENS };
    BrotliState state_{BrotliState::FIND_MATCHES};

    size_t WINDOW_SIZE;
    size_t SLIDE_SIZE;
    size_t MIN_MATCH;
    size_t MAX_MATCH;
    size_t HASH_SIZE;
    size_t MAX_CHAIN_LENGTH;
    static constexpr size_t MAX_BLOCK_TOKENS = 16384;

    std::vector<uint8_t> window_;
    std::vector<uint16_t> head_;
    std::vector<uint16_t> prev_;

    size_t cursor_{0};
    size_t lookahead_{0};

    std::vector<BrotliToken> token_buffer_;
    size_t token_flush_idx_{0};

    std::unique_ptr<HuffmanTree> lit_tree0_;  // after-literal context
    std::unique_ptr<HuffmanTree> lit_tree1_;  // after-match context
    std::unique_ptr<HuffmanTree> len_tree_;
    std::unique_ptr<HuffmanTree> dist_tree_;

    std::vector<HuffmanCode> lit_dict0_;
    std::vector<HuffmanCode> lit_dict1_;
    std::vector<HuffmanCode> len_dict_;
    std::vector<HuffmanCode> dist_dict_;

    auto getHash(size_t pos) -> uint16_t;
    auto fillWindow(void) -> void;
    auto slideWindow(void) -> void;

    auto handleFindMatches(AlgorithmStatus& status, bool is_last_chunk) -> void;
    auto handleBuildTree(AlgorithmStatus& status, bool is_last_chunk) -> void;
    auto handleFlushTokens(AlgorithmStatus& status, bool is_last_chunk) -> void;

    void getLengthCode(size_t length, uint8_t& code, uint8_t& extra_bits,
                       uint16_t& extra_val);
    void getDistCode(size_t dist, uint8_t& code, uint8_t& extra_bits,
                     uint16_t& extra_val);
};

class BrotliDecompress : public AlgorithmBase {
public:
    BrotliDecompress();

    auto reset(void) -> void override;

protected:
    auto handle(AlgorithmStatus& status, bool is_last_chunk) -> void override;

private:
    std::vector<uint8_t> output_buf_;

    node* lit_root0_{nullptr};
    node* lit_root1_{nullptr};
    node* len_root_{nullptr};
    node* dist_root_{nullptr};

    node* lit_cursor_{nullptr};
    node* len_cursor_{nullptr};
    node* dist_cursor_{nullptr};

    bool prev_was_match_{false};

    enum class DecodeState {
        READ_LIT_TREE0,
        READ_LIT_TREE1,
        READ_LEN_TREE,
        READ_DIST_TREE,
        DECODE_TOKENS,
        COPY_MATCH
    };
    DecodeState decode_state_{DecodeState::READ_LIT_TREE0};

    uint16_t pending_length_{0};
    uint16_t pending_dist_{0};

    static constexpr size_t LENGTH_BASES[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
    };
    static constexpr uint8_t LENGTH_EXTRA[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
    };
    static constexpr size_t DIST_BASES[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
    };
    static constexpr uint8_t DIST_EXTRA[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
    };

    auto readHuffmanTree(node*& root, size_t symbol_bits) -> bool;
    void decodeLengthCode(uint16_t symbol, uint16_t& length, uint8_t& extra_bits);
    void decodeDistCode(uint16_t symbol, uint16_t& dist, uint8_t& extra_bits);
    void destroyTree(node* n);
};

}  // namespace compressor::algorithm

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "HuffmanTree.hpp"
#include "IAlgorithm.hpp"

namespace compressor::algorithm {

struct MyFlateToken {
    bool is_literal;

    uint16_t code;

    uint8_t length_extra_bits;
    uint16_t length_extra_val;

    uint8_t dist_code;
    uint8_t dist_extra_bits;
    uint16_t dist_extra_val;
};

class MyFlate : public AlgorithmBase {
public:
    MyFlate(size_t search_size = 32768, size_t lookahead_size = 258,
            size_t min_match = 3, size_t max_chain_length = 256,
            size_t dp_sub_match_max = 6);

    auto reset(void) -> void override;

protected:
    auto handle(AlgorithmStatus& algorithm_status, bool is_last_chunk)
        -> void override;

private:
    enum class MyFlateState { FIND_MATCHES, BUILD_TREE, FLUSH_TOKENS };

    MyFlateState state_{MyFlateState::FIND_MATCHES};

    auto handleFindMatches(AlgorithmStatus& status, bool is_last_chunk) -> void;
    auto handleBuildTree(AlgorithmStatus& status, bool is_last_chunk) -> void;
    auto handleFlushTokens(AlgorithmStatus& status, bool is_last_chunk) -> void;

    using StateHandler = void (MyFlate::*)(AlgorithmStatus&, bool);
    static constexpr StateHandler kStateHandlers[3] = {
        &MyFlate::handleFindMatches, &MyFlate::handleBuildTree,
        &MyFlate::handleFlushTokens};

    size_t SEARCH_SIZE;
    size_t LOOKAHEAD_SIZE;
    size_t MIN_MATCH;
    size_t MAX_CHAIN_LENGTH;
    size_t DP_SUB_MATCH_MAX;
    static constexpr size_t MAX_MATCH = 258;
    static constexpr size_t DISTANCE_DICTIONARY_SIZE = 30;
    static constexpr size_t DISTANCE_SYMBOL_BITS = 5;
    static constexpr size_t MAX_BLOCK_TOKENS = 16384;
    static constexpr uint32_t NULL_PTR = 0xffffffff;

    std::vector<uint8_t> window_;
    size_t data_len_{0};
    size_t cursor_{0};

    std::vector<MyFlateToken> token_buffer_;
    size_t token_flush_idx_{0};

    std::unique_ptr<HuffmanTree> huffman_tree_;
    std::unique_ptr<HuffmanTree> dist_tree_;

    std::vector<HuffmanCode> dictionary_;
    std::vector<HuffmanCode> dist_dictionary_;

    std::vector<uint32_t> head_;
    std::vector<uint32_t> prev_;

    inline auto getHash(size_t pos) -> uint16_t {
        return ((window_[pos] << 10) ^ (window_[pos + 1] << 5) ^
                window_[pos + 2]) &
               (SEARCH_SIZE - 1);
    }

    void getLengthCode(size_t length, uint16_t& code, uint8_t& extra_bits,
                       uint16_t& extra_val);
    void getDistCode(size_t dist, uint8_t& code, uint8_t& extra_bits,
                     uint16_t& extra_val);
};

}  // namespace compressor::algorithm

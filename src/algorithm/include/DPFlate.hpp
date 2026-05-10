#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "Deflate.hpp"
#include "HuffmanTree.hpp"
#include "IAlgorithm.hpp"
#include "Inflate.hpp"

namespace compressor::algorithm {

class DPFlate : public AlgorithmBase {
public:
    DPFlate(size_t search_size = 32768, size_t lookahead_size = 258,
            size_t min_match = 3, size_t dp_top = 4,
            size_t dp_sub_match_max = 6);

    auto reset(void) -> void override;

protected:
    auto handle(AlgorithmStatus& algorithm_status, bool is_last_chunk)
        -> void override;

private:
    enum class DPFlateState { COLLECT_INPUT, BUILD_TREE, FLUSH_TOKENS };

    DPFlateState state_{DPFlateState::COLLECT_INPUT};

    auto handleCollectInput(AlgorithmStatus& status, bool is_last_chunk) -> void;
    auto handleBuildTree(AlgorithmStatus& status, bool is_last_chunk) -> void;
    auto handleFlushTokens(AlgorithmStatus& status, bool is_last_chunk) -> void;

    using StateHandler = void (DPFlate::*)(AlgorithmStatus&, bool);
    static constexpr StateHandler kStateHandlers[3] = {
        &DPFlate::handleCollectInput, &DPFlate::handleBuildTree,
        &DPFlate::handleFlushTokens};

    size_t SEARCH_SIZE;
    size_t LOOKAHEAD_SIZE;
    size_t MIN_MATCH;
    size_t DP_TOP;
    size_t DP_SUB_MATCH_MAX;

    int match_engine_{1}; // 0 = KMP, 1 = HashChain
public:
    void set_match_engine(int v) { match_engine_ = v; }
    int get_match_engine() const { return match_engine_; }
private:

    std::vector<uint8_t> input_buffer_;

    std::vector<Token> token_buffer_;
    size_t token_flush_idx_{0};

    std::unique_ptr<HuffmanTree> huffman_tree_;
    std::unique_ptr<HuffmanTree> dist_tree_;

    std::vector<HuffmanCode> dictionary_;
    std::vector<HuffmanCode> dist_dictionary_;

    static constexpr size_t DEFLATE_ALPHABET_SIZE = 286;
    static constexpr size_t DISTANCE_DICTIONARY_SIZE = 30;
    static constexpr size_t DEFLATE_SYMBOL_BITS = 9;
    static constexpr size_t DISTANCE_SYMBOL_BITS = 5;

    void getLengthCode(size_t length, uint16_t& code, uint8_t& extra_bits,
                       uint16_t& extra_val);
    void getDistCode(size_t dist, uint8_t& code, uint8_t& extra_bits,
                     uint16_t& extra_val);
};

using DPFlateDecompress = Inflate;

}  // namespace compressor::algorithm
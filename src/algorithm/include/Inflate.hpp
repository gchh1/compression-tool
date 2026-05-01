#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "HuffmanTree.hpp"
#include "IAlgorithm.hpp"

namespace compressor::algorithm {

class Inflate : public AlgorithmBase {
   public:
    Inflate();
    auto reset(void) -> void override;

   protected:
    auto handle(AlgorithmStatus& status, bool is_last_chunk) -> void override;

   private:
    static constexpr uint16_t DICTIONARY_SIZE = 32768;
    static constexpr uint16_t DICT_MASK = DICTIONARY_SIZE - 1;

    // Length code lookup tables (matching Deflate::getLengthCode)
    static const uint16_t kLengthBase[];
    static const uint8_t kLengthExtraBits[];

    // Distance code lookup tables (matching Deflate::getDistCode)
    static const uint16_t kDistanceBase[];
    static const uint8_t kDistanceExtraBits[];

    // LZSS sliding window (32KB circular buffer)
    std::vector<uint8_t> window_;
    size_t decode_pos_{0};

    // State machine
    enum class InflateState {
        READ_BLOCK_HEADER,
        READ_LL_TREE,
        READ_D_TREE,
        DECODE_TOKEN,
        DECODE_DISTANCE
    };
    InflateState inflate_state_{InflateState::READ_BLOCK_HEADER};
    bool is_last_block_{false};

    // Huffman trees and traversal cursors
    std::unique_ptr<HuffmanTree> ll_tree_;
    std::unique_ptr<HuffmanTree> dist_tree_;
    node* ll_cursor_{nullptr};
    node* dist_cursor_{nullptr};

    // Match copy state
    uint16_t copy_length_{0};
    uint16_t copy_distance_{0};

    // Pending output (literal decoded but couldn't write due to full output)
    uint8_t pending_byte_{0};
    bool has_pending_{false};

    // Pending extra bits (when reader runs out mid-read)
    uint8_t pending_extra_{0};
    uint8_t pending_extra_bits_{0};
    bool has_pending_extra_{false};
};

}  // namespace compressor::algorithm

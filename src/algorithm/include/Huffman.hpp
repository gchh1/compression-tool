#pragma once

// Include lib here

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "HuffmanTree.hpp"

namespace compressor::algorithm {
/**
 * @brief Huffman compress algorithm
 *
 */
class Huffman {
   public:
    auto compress(std::span<const uint8_t> read, std::span<uint8_t> write,
                  bool is_last) -> size_t;

    auto decompress(std::span<const uint8_t> read, std::span<uint8_t> write)
        -> size_t;

    auto reset(void) -> void;

   private:
    // Encode state
    uint64_t encode_buffer_{0};
    uint8_t encode_buffer_idx_{0};

    // Decode state
    uint64_t decode_buffer_{0};
    uint8_t decode_buffer_idx_{0};

    enum class DecodeState { READ_SIZE, READ_TREE, DECODE_DATA };
    DecodeState decode_state_{DecodeState::READ_SIZE};

    uint32_t target_size_{0};
    uint32_t current_decode_size_{0};
    node* current_cursor_{nullptr};

    std::unique_ptr<HuffmanTree> huffman_tree_{nullptr};
};

}  // namespace compressor::algorithm

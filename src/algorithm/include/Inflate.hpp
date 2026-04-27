/**
 * @file Inflate.hpp
 * @author yhc
 * @brief Handling `decompression`, or `decompression` deflate
 * @version 0.1
 * @date 2026-04-24
 *
 * @copyright Copyright (c) 2026
 *
 */

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "Deflate.hpp"
#include "HuffmanTree.hpp"

namespace compressor::algorithm {
class Inflate : public AlgorithmBase {
   public:
    Inflate();

    auto reset(void) -> void override;

   protected:
    auto handle(AlgorithmStatus& status, bool is_last_chunk) -> void override;

   private:
    static constexpr uint16_t DICTIONARY_SIZE = 32768;

    std::vector<uint8_t> window_;  // 32KB
    size_t decode_pos_{0};

    bool is_last_block_{false};

    // ===================================
    // State machine
    // ===================================
    enum class InflateState { READ_BLOCK_HEADER, READ_TREE, DECODE_TOKENS };
    InflateState inflate_state_{InflateState::READ_BLOCK_HEADER};

    std::unique_ptr<HuffmanTree> huffman_tree_{nullptr};
    node* cursor_{nullptr};

    uint64_t decode_buffer_{0};
    uint8_t decode_buffer_idx_{0};

    uint16_t copy_length_{0};
    uint16_t copy_distance_{0};
};
}  // namespace compressor::algorithm
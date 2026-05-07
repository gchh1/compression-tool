#pragma once

#include <cstddef>
#include <cstdint>
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
    static constexpr size_t DISTANCE_DICTIONARY_SIZE = 30;
    static constexpr size_t DISTANCE_SYMBOL_BITS = 5;

    std::vector<uint8_t> output_buf_;

    node* lit_root_{nullptr};
    node* dist_root_{nullptr};
    node* lit_cursor_{nullptr};
    node* dist_cursor_{nullptr};

    enum class DecodeState { READ_TREES, READ_BLOCK_HEADER, DECODE_TOKENS, COPY_MATCH, STORED_COPY };
    DecodeState decode_state_{DecodeState::READ_TREES};

    uint16_t pending_length_{0};
    uint16_t pending_dist_{0};
    size_t stored_bytes_remaining_{0};

    auto readHuffmanTree(node*& root, size_t symbol_bits) -> bool;

    void decodeLengthCode(uint16_t symbol, uint16_t& length, uint8_t& extra_bits);
    void decodeDistCode(uint16_t symbol, uint16_t& dist, uint8_t& extra_bits);

    void destroyTree(node* n);

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
};

}  // namespace compressor::algorithm

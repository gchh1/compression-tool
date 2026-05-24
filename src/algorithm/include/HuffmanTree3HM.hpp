#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "BitReader.hpp"
#include "BitWriter.hpp"
#include "HuffmanTree.hpp"

namespace compressor::algorithm {

class HuffmanTree3HM {
public:
    HuffmanTree3HM() = default;

    void buildTrees(
        const std::vector<uint32_t>& literal_freq,
        const std::vector<uint32_t>& offset_freq,
        const std::vector<uint32_t>& length_freq,
        size_t offset_count, size_t length_count,
        size_t offset_bits, size_t length_bits,
        size_t offset_chunk_bits, size_t length_chunk_bits);

    void serialize(utils::BitWriter& writer) const;

    void deserialize(utils::BitReader& reader);

    void deserialize(utils::BitReader& reader,
                     size_t offset_count, size_t length_count,
                     size_t offset_bits, size_t length_bits,
                     size_t offset_chunk_bits, size_t length_chunk_bits);

    void encodeLiteral(uint8_t byte, utils::BitWriter& writer) const;

    void encodeMatch(uint16_t offset, uint16_t length,
                     utils::BitWriter& writer) const;

    void encodeRunHeader(uint16_t run_length,
                         utils::BitWriter& writer) const;

    uint16_t decodeOffset(utils::BitReader& reader) const;

    uint16_t decodeRunLength(utils::BitReader& reader) const;

    uint8_t decodeLiteral(utils::BitReader& reader) const;

    uint16_t decodeMatchLength(utils::BitReader& reader) const;

    size_t getTreeSize() const;

    const HuffmanTree& literalTree() const { return *literal_tree_; }
    const HuffmanTree& offsetTree() const { return *offset_tree_; }
    const HuffmanTree& lengthTree() const { return *length_tree_; }
    const std::vector<HuffmanCode>& litDict() const { return literal_dict_; }
    const std::vector<HuffmanCode>& offDict() const { return offset_dict_; }
    const std::vector<HuffmanCode>& lenDict() const { return length_dict_; }

    size_t offsetBits() const { return offset_bits_; }
    size_t lengthBits() const { return length_bits_; }
    size_t offsetChunkBits() const { return offset_chunk_bits_; }
    size_t lengthChunkBits() const { return length_chunk_bits_; }
    size_t offsetCount() const { return offset_count_; }
    size_t lengthCount() const { return length_count_; }

private:
    size_t offset_count_{0};
    size_t length_count_{0};
    size_t offset_bits_{0};
    size_t length_bits_{0};
    size_t offset_chunk_bits_{0};
    size_t length_chunk_bits_{0};

    std::unique_ptr<HuffmanTree> literal_tree_;
    std::unique_ptr<HuffmanTree> offset_tree_;
    std::unique_ptr<HuffmanTree> length_tree_;

    std::vector<HuffmanCode> literal_dict_;
    std::vector<HuffmanCode> offset_dict_;
    std::vector<HuffmanCode> length_dict_;

    void encodeMultiLevel(uint16_t value, size_t value_bits,
                          const HuffmanTree& tree,
                          const std::vector<HuffmanCode>& dict,
                          size_t chunk_bits,
                          utils::BitWriter& writer) const;

    uint16_t decodeMultiLevel(size_t value_bits,
                              const HuffmanTree& tree,
                              size_t chunk_bits,
                              utils::BitReader& reader) const;
};

}  // namespace compressor::algorithm
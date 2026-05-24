#include "HuffmanTree3HM.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "BitReader.hpp"
#include "BitWriter.hpp"
#include "DebugLog.hpp"
#include "HuffmanTree.hpp"

namespace compressor::algorithm {

void HuffmanTree3HM::buildTrees(
    const std::vector<uint32_t>& literal_freq,
    const std::vector<uint32_t>& offset_freq,
    const std::vector<uint32_t>& length_freq,
    size_t offset_count, size_t length_count,
    size_t offset_bits, size_t length_bits,
    size_t offset_chunk_bits, size_t length_chunk_bits) {

    offset_count_ = offset_count;
    length_count_ = length_count;
    offset_bits_ = offset_bits;
    length_bits_ = length_bits;
    offset_chunk_bits_ = offset_chunk_bits;
    length_chunk_bits_ = length_chunk_bits;

    auto ensure_all_symbols = [](std::vector<uint32_t> freq, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            if (freq[i] == 0) freq[i] = 1;
        }
        return freq;
    };

    literal_tree_ = std::make_unique<HuffmanTree>(literal_freq, 256, 8);
    offset_tree_ = std::make_unique<HuffmanTree>(
        ensure_all_symbols(offset_freq, offset_count_), offset_count_, offset_chunk_bits_);
    length_tree_ = std::make_unique<HuffmanTree>(
        ensure_all_symbols(length_freq, length_count_), length_count_, length_chunk_bits_);

    literal_dict_ = literal_tree_->buildDictionary();
    offset_dict_ = offset_tree_->buildDictionary();
    length_dict_ = length_tree_->buildDictionary();
}

void HuffmanTree3HM::serialize(utils::BitWriter& writer) const {
    writer.writeBits(static_cast<uint32_t>(offset_bits_), 8);
    writer.writeBits(static_cast<uint32_t>(length_bits_), 8);
    writer.writeBits(static_cast<uint32_t>(offset_chunk_bits_), 8);
    writer.writeBits(static_cast<uint32_t>(length_chunk_bits_), 8);
    literal_tree_->serializeTree(writer);
    offset_tree_->serializeTree(writer);
    length_tree_->serializeTree(writer);
}

void HuffmanTree3HM::deserialize(utils::BitReader& reader) {
    offset_bits_ = reader.readBits(8);
    length_bits_ = reader.readBits(8);
    offset_chunk_bits_ = reader.readBits(8);
    length_chunk_bits_ = reader.readBits(8);

    offset_count_ = size_t{1} << offset_chunk_bits_;
    length_count_ = size_t{1} << length_chunk_bits_;

    DEBUG_LOG("[HuffmanTree3HM] deserialize: offset_bits=%zu length_bits=%zu offset_chunk=%zu(%zu symbols) length_chunk=%zu(%zu symbols)",
              offset_bits_, length_bits_, offset_chunk_bits_, offset_count_, length_chunk_bits_, length_count_);

    literal_tree_ = std::make_unique<HuffmanTree>(reader, 256, 8);
    offset_tree_ = std::make_unique<HuffmanTree>(reader, offset_count_, offset_chunk_bits_);
    length_tree_ = std::make_unique<HuffmanTree>(reader, length_count_, length_chunk_bits_);

    literal_dict_ = literal_tree_->buildDictionary();
    offset_dict_ = offset_tree_->buildDictionary();
    length_dict_ = length_tree_->buildDictionary();

    DEBUG_LOG("[HuffmanTree3HM] deserialize done: literal_tree_size=%zu offset_tree_size=%zu length_tree_size=%zu",
              literal_tree_->getTreeSize(), offset_tree_->getTreeSize(), length_tree_->getTreeSize());
}

void HuffmanTree3HM::deserialize(utils::BitReader& reader,
                                 size_t offset_count, size_t length_count,
                                 size_t offset_bits, size_t length_bits,
                                 size_t offset_chunk_bits, size_t length_chunk_bits) {
    offset_count_ = offset_count;
    length_count_ = length_count;
    offset_bits_ = offset_bits;
    length_bits_ = length_bits;
    offset_chunk_bits_ = offset_chunk_bits;
    length_chunk_bits_ = length_chunk_bits;

    literal_tree_ = std::make_unique<HuffmanTree>(reader, 256, 8);
    offset_tree_ = std::make_unique<HuffmanTree>(reader, offset_count_, offset_chunk_bits_);
    length_tree_ = std::make_unique<HuffmanTree>(reader, length_count_, length_chunk_bits_);

    literal_dict_ = literal_tree_->buildDictionary();
    offset_dict_ = offset_tree_->buildDictionary();
    length_dict_ = length_tree_->buildDictionary();
}

void HuffmanTree3HM::encodeLiteral(uint8_t byte,
                                    utils::BitWriter& writer) const {
    writeHuffmanCode(writer, literal_dict_[byte]);
}

void HuffmanTree3HM::encodeMultiLevel(uint16_t value, size_t value_bits,
                                     const HuffmanTree& tree,
                                     const std::vector<HuffmanCode>& dict,
                                     size_t chunk_bits,
                                     utils::BitWriter& writer) const {

    size_t num_chunks = (value_bits + chunk_bits - 1) / chunk_bits;
    const size_t mask = (size_t{1} << chunk_bits) - 1;
    for (size_t i = 0; i < num_chunks; ++i) {
        size_t chunk = (static_cast<size_t>(value) >> (i * chunk_bits)) & mask;
        writeHuffmanCode(writer, dict[chunk]);
    }
}

uint16_t HuffmanTree3HM::decodeMultiLevel(size_t value_bits,
                                          const HuffmanTree& tree,
                                          size_t chunk_bits,
                                          utils::BitReader& reader) const {

    size_t num_chunks = (value_bits + chunk_bits - 1) / chunk_bits;
    uint16_t value = 0;
    size_t last_chunk_valid_bits = value_bits % chunk_bits;
    if (last_chunk_valid_bits == 0) {
        last_chunk_valid_bits = chunk_bits;
    }

    for (size_t i = 0; i < num_chunks; ++i) {
        node* cursor = tree.getRoot();
        while (cursor && !cursor->isLeaf()) {
            uint8_t bit = reader.readBit();
            cursor = (bit == 0) ? cursor->left : cursor->right;
        }
        size_t chunk = cursor ? cursor->symbol : 0;

        if (i == num_chunks - 1 && last_chunk_valid_bits < chunk_bits) {
            chunk &= (size_t{1} << last_chunk_valid_bits) - 1;
        }
        value |= static_cast<uint16_t>(chunk << (i * chunk_bits));
    }
    return value;
}

void HuffmanTree3HM::encodeMatch(uint16_t offset, uint16_t length,
                                  utils::BitWriter& writer) const {
    encodeMultiLevel(offset, offset_bits_, *offset_tree_, offset_dict_,
                     offset_chunk_bits_, writer);
    encodeMultiLevel(length, length_bits_, *length_tree_, length_dict_,
                     length_chunk_bits_, writer);
}

void HuffmanTree3HM::encodeRunHeader(uint16_t run_length,
                                      utils::BitWriter& writer) const {
    encodeMultiLevel(0, offset_bits_, *offset_tree_, offset_dict_,
                     offset_chunk_bits_, writer);
    encodeMultiLevel(run_length, length_bits_, *length_tree_, length_dict_,
                     length_chunk_bits_, writer);
}

uint16_t HuffmanTree3HM::decodeOffset(utils::BitReader& reader) const {
    return decodeMultiLevel(offset_bits_, *offset_tree_, offset_chunk_bits_, reader);
}

uint16_t HuffmanTree3HM::decodeRunLength(utils::BitReader& reader) const {
    return decodeMultiLevel(length_bits_, *length_tree_, length_chunk_bits_, reader);
}

uint8_t HuffmanTree3HM::decodeLiteral(utils::BitReader& reader) const {
    node* cursor = literal_tree_->getRoot();
    while (cursor && !cursor->isLeaf()) {
        uint8_t bit = reader.readBit();
        cursor = (bit == 0) ? cursor->left : cursor->right;
    }
    return cursor ? static_cast<uint8_t>(cursor->symbol) : 0;
}

uint16_t HuffmanTree3HM::decodeMatchLength(utils::BitReader& reader) const {
    return decodeMultiLevel(length_bits_, *length_tree_, length_chunk_bits_, reader);
}

size_t HuffmanTree3HM::getTreeSize() const {
    size_t total = 32;
    if (literal_tree_) total += literal_tree_->getTreeSize();
    if (offset_tree_) total += offset_tree_->getTreeSize();
    if (length_tree_) total += length_tree_->getTreeSize();
    return total;
}

}  // namespace compressor::algorithm
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "BitProcessor.hpp"
#include "HuffmanTree.hpp"

namespace compressor::algorithm {

struct HuffmanTree3HMConfig {
    size_t offset_range{1024};
    size_t length_range{1024};
    size_t max_literal_run{1023};
    uint32_t chunk_bits{10};
    uint32_t max_offset_bits{16};
    uint32_t max_length_bits{9};

    size_t literal_dict_size() const { return 256; }
    size_t offset_dict_size() const { return offset_range; }
    size_t length_dict_size() const { return length_range; }

    uint32_t offset_chunks() const {
        return (max_offset_bits + chunk_bits - 1) / chunk_bits;
    }
    uint32_t length_chunks() const {
        return (max_length_bits + chunk_bits - 1) / chunk_bits;
    }
    uint32_t chunk_mask() const {
        return (1u << chunk_bits) - 1;
    }
};

class HuffmanTree3HM {
public:
    HuffmanTree3HM() = default;

    explicit HuffmanTree3HM(const HuffmanTree3HMConfig& cfg) : config_(cfg) {}

    void buildTrees(const std::vector<uint32_t>& lit_freq,
                    const std::vector<uint32_t>& off_freq,
                    const std::vector<uint32_t>& len_freq) {
        literal_tree_ = HuffmanTree(lit_freq, config_.literal_dict_size(), 8);
        offset_tree_ = HuffmanTree(off_freq, config_.offset_dict_size(), 16);
        length_tree_ = HuffmanTree(len_freq, config_.length_dict_size(), 16);

        lit_dict_ = literal_tree_.buildDictionary();
        off_dict_ = offset_tree_.buildDictionary();
        len_dict_ = length_tree_.buildDictionary();
    }

    void encodeLiteral(compressor::utils::BitWriter& writer,
                       uint8_t literal) const {
        writeHuffmanCode(writer, lit_dict_[literal]);
    }

    void encodeMatch(compressor::utils::BitWriter& writer,
                     uint32_t offset, uint32_t length) const {
        uint32_t oc = config_.offset_chunks();
        uint32_t lc = config_.length_chunks();
        uint32_t mask = config_.chunk_mask();

        for (uint32_t i = 0; i < oc; ++i) {
            uint32_t chunk = (offset >> (i * config_.chunk_bits)) & mask;
            writeHuffmanCode(writer, off_dict_[chunk]);
        }
        for (uint32_t i = 0; i < lc; ++i) {
            uint32_t chunk = (length >> (i * config_.chunk_bits)) & mask;
            writeHuffmanCode(writer, len_dict_[chunk]);
        }
    }

    uint8_t decodeLiteral(compressor::utils::BitReader& reader) const {
        return static_cast<uint8_t>(readHuffmanSymbol(reader, literal_tree_.getRoot()));
    }

    uint32_t decodeOffset(compressor::utils::BitReader& reader) const {
        uint32_t oc = config_.offset_chunks();
        uint32_t offset = 0;
        for (uint32_t i = 0; i < oc; ++i) {
            uint16_t sym = readHuffmanSymbol(reader, offset_tree_.getRoot());
            offset |= (static_cast<uint32_t>(sym) << (i * config_.chunk_bits));
        }
        return offset;
    }

    uint32_t decodeMatchLength(compressor::utils::BitReader& reader) const {
        uint32_t lc = config_.length_chunks();
        uint32_t length = 0;
        for (uint32_t i = 0; i < lc; ++i) {
            uint16_t sym = readHuffmanSymbol(reader, length_tree_.getRoot());
            length |= (static_cast<uint32_t>(sym) << (i * config_.chunk_bits));
        }
        return length;
    }

    void serialize(compressor::utils::BitWriter& writer) const {
        writer.writeBits(config_.offset_range, 32);
        writer.writeBits(config_.length_range, 32);
        writer.writeBits(config_.chunk_bits, 32);
        writer.writeBits(config_.max_offset_bits, 32);
        writer.writeBits(config_.max_length_bits, 32);
        literal_tree_.serialize(writer);
        offset_tree_.serialize(writer);
        length_tree_.serialize(writer);
    }

    void deserialize(compressor::utils::BitReader& reader) {
        uint64_t val;
        reader.readBits(val, 32); config_.offset_range = static_cast<size_t>(val);
        reader.readBits(val, 32); config_.length_range = static_cast<size_t>(val);
        reader.readBits(val, 32); config_.chunk_bits = static_cast<uint32_t>(val);
        reader.readBits(val, 32); config_.max_offset_bits = static_cast<uint32_t>(val);
        reader.readBits(val, 32); config_.max_length_bits = static_cast<uint32_t>(val);

        literal_tree_ = HuffmanTree();
        offset_tree_ = HuffmanTree();
        length_tree_ = HuffmanTree();

        new (&literal_tree_) HuffmanTree(
            std::vector<uint32_t>(config_.literal_dict_size(), 1),
            config_.literal_dict_size(), 8);
        literal_tree_.deserialize(reader);

        new (&offset_tree_) HuffmanTree(
            std::vector<uint32_t>(config_.offset_dict_size(), 1),
            config_.offset_dict_size(), 16);
        offset_tree_.deserialize(reader);

        new (&length_tree_) HuffmanTree(
            std::vector<uint32_t>(config_.length_dict_size(), 1),
            config_.length_dict_size(), 16);
        length_tree_.deserialize(reader);

        lit_dict_ = literal_tree_.buildDictionary();
        off_dict_ = offset_tree_.buildDictionary();
        len_dict_ = length_tree_.buildDictionary();
    }

    size_t getTotalTreeSize() const {
        size_t literal_bits = literal_tree_.getTreeSize();
        size_t offset_bits = offset_tree_.getTreeSize();
        size_t length_bits = length_tree_.getTreeSize();
        return 160 + literal_bits + offset_bits + length_bits;
    }

    const HuffmanTree3HMConfig& config() const { return config_; }

const HuffmanTree& literalTree() const { return literal_tree_; }
    const HuffmanTree& offsetTree() const { return offset_tree_; }
    const HuffmanTree& lengthTree() const { return length_tree_; }
    const std::vector<HuffmanCode>& litDict() const { return lit_dict_; }
    const std::vector<HuffmanCode>& offDict() const { return off_dict_; }
    const std::vector<HuffmanCode>& lenDict() const { return len_dict_; }

private:
    HuffmanTree3HMConfig config_;
    HuffmanTree literal_tree_;
    HuffmanTree offset_tree_;
    HuffmanTree length_tree_;

    std::vector<HuffmanCode> lit_dict_;
    std::vector<HuffmanCode> off_dict_;
    std::vector<HuffmanCode> len_dict_;
};

}  // namespace compressor::algorithm
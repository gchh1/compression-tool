#pragma once

#include <cstddef>
#include <cstdint>
#include <queue>
#include <utility>
#include <vector>

#include "BitReader.hpp"
#include "BitWriter.hpp"

namespace compressor::algorithm {

struct HuffmanCode {
    uint64_t code;
    uint16_t length;
    std::vector<uint8_t> bits;

    HuffmanCode(uint64_t c = 0, uint16_t len = 0, std::vector<uint8_t> b = {})
        : code(c), length(len), bits(std::move(b)) {}
};

struct node {
    uint16_t symbol;
    uint32_t freq;
    node* left;
    node* right;

    node(uint16_t sym = 0, uint32_t f = 0)
        : symbol(sym), freq(f), left(nullptr), right(nullptr) {}
    node(node* l, node* r)
        : symbol(0), freq(l->freq + r->freq), left(l), right(r) {}
    bool isLeaf() const { return left == nullptr && right == nullptr; }
};

struct Compare {
    bool operator()(const node* a, const node* b) const {
        return a->freq > b->freq;
    }
};

class HuffmanTree {
public:
    HuffmanTree();

    HuffmanTree(std::span<const uint8_t> symbols);

    HuffmanTree(const std::vector<uint32_t>& freq_map,
                size_t dictionary_size,
                size_t symbol_bits);

    HuffmanTree(node* root, size_t dictionary_size, size_t symbol_bits);

    HuffmanTree(utils::BitReader& reader, size_t dictionary_size,
                size_t symbol_bits);

    explicit HuffmanTree(utils::BitReader& reader);

    ~HuffmanTree();

    HuffmanTree(const HuffmanTree&) = delete;
    HuffmanTree& operator=(const HuffmanTree&) = delete;
    HuffmanTree(HuffmanTree&& other) noexcept;
    HuffmanTree& operator=(HuffmanTree&& other) noexcept;

    node* getRoot() const { return root_; }
    size_t getDictionarySize() const { return dictionary_size_; }
    size_t getSymbolBits() const { return symbol_bits_; }
    size_t getTreeSize() const { return tree_size_; }

    void buildTree(const std::vector<uint32_t>& freq_map);

    std::vector<HuffmanCode> buildDictionary() const;

    void serializeTree(utils::BitWriter& writer) const;

    void serialize(utils::BitWriter& writer) const { serializeTree(writer); }

    void deserialize(utils::BitReader& reader);

private:
    node* root_{nullptr};
    size_t dictionary_size_{256};
    size_t symbol_bits_{8};
    size_t tree_size_{0};

    size_t calcSerializedBits(const node* n) const;

    void generateCodes(node* n, uint64_t current_code,
                       uint16_t current_length,
                       std::vector<uint8_t>& current_bits,
                       std::vector<HuffmanCode>& dict) const;

    void serializeNode(utils::BitWriter& writer, node* n) const;
};

inline void writeHuffmanCode(utils::BitWriter& writer, const HuffmanCode& code) {
    for (uint8_t bit : code.bits) {
        writer.writeBit(bit);
    }
}

inline uint16_t readHuffmanSymbol(utils::BitReader& reader, node* root) {
    node* cursor = root;
    while (cursor && !cursor->isLeaf()) {
        uint8_t bit = reader.readBit();
        cursor = (bit == 0) ? cursor->left : cursor->right;
    }
    return cursor ? cursor->symbol : 0;
}

}  // namespace compressor::algorithm
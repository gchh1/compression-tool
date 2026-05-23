#pragma once

#include <cstddef>
#include <cstdint>
#include <queue>
#include <utility>
#include <vector>

#include "BitProcessor.hpp"

namespace compressor::algorithm {

struct HuffmanCode {
    uint64_t code;
    uint16_t length;
    std::vector<uint8_t> bits;

    HuffmanCode(uint64_t c = 0, uint16_t len = 0, std::vector<uint8_t> b = {})
        : code(c), length(len), bits(std::move(b)) {}
};

struct HuffmanNode {
    uint16_t symbol;
    uint32_t freq;
    HuffmanNode* left;
    HuffmanNode* right;

    HuffmanNode(uint16_t sym = 0, uint32_t f = 0)
        : symbol(sym), freq(f), left(nullptr), right(nullptr) {}
    HuffmanNode(HuffmanNode* l, HuffmanNode* r)
        : symbol(0), freq(l->freq + r->freq), left(l), right(r) {}
    bool isLeaf() const { return left == nullptr && right == nullptr; }
};

static inline void destroyHuffmanNode(HuffmanNode* n) {
    if (!n) return;
    destroyHuffmanNode(n->left);
    destroyHuffmanNode(n->right);
    delete n;
}

struct HuffmanNodeCompare {
    bool operator()(const HuffmanNode* a, const HuffmanNode* b) const {
        return a->freq > b->freq;
    }
};

class HuffmanTree {
public:
    HuffmanTree() = default;

    HuffmanTree(const std::vector<uint32_t>& freq_map,
                size_t dictionary_size,
                size_t symbol_bits)
        : dictionary_size_(dictionary_size), symbol_bits_(symbol_bits) {
        buildTree(freq_map);
    }

    ~HuffmanTree() { destroyHuffmanNode(root_); }

    HuffmanTree(const HuffmanTree&) = delete;
    HuffmanTree& operator=(const HuffmanTree&) = delete;
    HuffmanTree(HuffmanTree&& other) noexcept
        : root_(std::exchange(other.root_, nullptr)),
          dictionary_size_(other.dictionary_size_),
          symbol_bits_(other.symbol_bits_),
          tree_size_(other.tree_size_) {}
    HuffmanTree& operator=(HuffmanTree&& other) noexcept {
        if (this != &other) {
            destroyHuffmanNode(root_);
            root_ = std::exchange(other.root_, nullptr);
            dictionary_size_ = other.dictionary_size_;
            symbol_bits_ = other.symbol_bits_;
            tree_size_ = other.tree_size_;
        }
        return *this;
    }

    HuffmanNode* getRoot() const { return root_; }
    size_t getDictionarySize() const { return dictionary_size_; }
    size_t getSymbolBits() const { return symbol_bits_; }
    size_t getTreeSize() const { return tree_size_; }

    void buildTree(const std::vector<uint32_t>& freq_map) {
        destroyHuffmanNode(root_);
        root_ = nullptr;

        std::priority_queue<HuffmanNode*, std::vector<HuffmanNode*>, HuffmanNodeCompare> pq;
        for (size_t i = 0; i < dictionary_size_ && i < freq_map.size(); ++i) {
            if (freq_map[i] > 0) {
                pq.push(new HuffmanNode(static_cast<uint16_t>(i), freq_map[i]));
            }
        }

        if (pq.empty()) {
            root_ = new HuffmanNode(
                new HuffmanNode(uint16_t{0}, uint32_t{0}),
                new HuffmanNode(uint16_t{1}, uint32_t{0}));
            tree_size_ = calcSerializedBits(root_);
            return;
        }
        if (pq.size() == 1) {
            HuffmanNode* only = pq.top();
            pq.pop();
            root_ = new HuffmanNode(only, new HuffmanNode(uint16_t{0}, uint32_t{0}));
            tree_size_ = calcSerializedBits(root_);
            return;
        }

        while (pq.size() >= 2) {
            HuffmanNode* left = pq.top(); pq.pop();
            HuffmanNode* right = pq.top(); pq.pop();
            pq.push(new HuffmanNode(left, right));
        }
        root_ = pq.top();
        tree_size_ = calcSerializedBits(root_);
    }

    std::vector<HuffmanCode> buildDictionary() const {
        std::vector<HuffmanCode> dict(dictionary_size_);
        std::vector<uint8_t> bits;
        generateCodes(root_, 0, 0, bits, dict);
        return dict;
    }

    void serialize(compressor::utils::BitWriter& writer) const {
        serializeNode(writer, root_);
    }

    void deserialize(compressor::utils::BitReader& reader) {
        destroyHuffmanNode(root_);
        root_ = deserializeNode(reader);
        tree_size_ = calcSerializedBits(root_);
    }

private:
    HuffmanNode* root_{nullptr};
    size_t dictionary_size_{0};
    size_t symbol_bits_{0};
    size_t tree_size_{0};

    size_t calcSerializedBits(const HuffmanNode* n) const {
        if (!n) return 0;
        if (n->isLeaf()) return 1 + symbol_bits_;
        return 1 + calcSerializedBits(n->left) + calcSerializedBits(n->right);
    }

    void generateCodes(HuffmanNode* n, uint64_t code, uint16_t length,
                       std::vector<uint8_t>& bits,
                       std::vector<HuffmanCode>& dict) const {
        if (!n) return;
        if (n->isLeaf()) {
            dict[n->symbol] = {code, length, bits};
            return;
        }
        bits.push_back(0);
        generateCodes(n->left, length < 64 ? (code << 1) : 0, length + 1, bits, dict);
        bits.back() = 1;
        generateCodes(n->right, length < 64 ? ((code << 1) | 1) : 0, length + 1, bits, dict);
        bits.pop_back();
    }

    void serializeNode(compressor::utils::BitWriter& writer, HuffmanNode* n) const {
        if (!n) return;
        if (n->isLeaf()) {
            writer.writeBits(1, 1);
            writer.writeBits(n->symbol, static_cast<int>(symbol_bits_));
        } else {
            writer.writeBits(0, 1);
            serializeNode(writer, n->left);
            serializeNode(writer, n->right);
        }
    }

    HuffmanNode* deserializeNode(compressor::utils::BitReader& reader) {
        uint64_t bit;
        reader.readBits(bit, 1);
        if (bit == 1) {
            uint64_t sym;
            reader.readBits(sym, static_cast<int>(symbol_bits_));
            return new HuffmanNode(static_cast<uint16_t>(sym), 0);
        }
        HuffmanNode* left = deserializeNode(reader);
        HuffmanNode* right = deserializeNode(reader);
        if (!left || !right) return nullptr;
        return new HuffmanNode(left, right);
    }
};

inline void writeHuffmanCode(compressor::utils::BitWriter& writer,
                             const HuffmanCode& code) {
    for (uint8_t bit : code.bits) {
        writer.writeBits(bit, 1);
    }
}

inline uint16_t readHuffmanSymbol(compressor::utils::BitReader& reader,
                                   HuffmanNode* root) {
    HuffmanNode* cursor = root;
    while (cursor && !cursor->isLeaf()) {
        uint64_t bit;
        reader.readBits(bit, 1);
        cursor = bit ? cursor->right : cursor->left;
    }
    return cursor ? cursor->symbol : 0;
}

}  // namespace compressor::algorithm
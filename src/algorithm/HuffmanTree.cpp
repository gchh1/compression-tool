// Include lib here
#include "HuffmanTree.hpp"

#include <cstddef>
#include <cstdint>
#include <queue>
#include <span>
#include <vector>

#include "BitWriter.hpp"

namespace compressor::algorithm {

/**
 * @brief Construct a new Huffman Tree:: Huffman Tree object
 *
 * @param symbols
 */
HuffmanTree::HuffmanTree(std::span<const uint8_t> symbols) {
    if (symbols.empty()) {
        return;
    }
    /* 1. Count the frequency of each symbol */
    std::vector<uint32_t> freq_map(256, 0);
    for (const uint8_t& item : symbols) {
        freq_map[item]++;
    }

    /* 2. Build the tree*/
    buildTree(freq_map);
}

/**
 * @brief Construct a new Huffman Tree:: Huffman Tree object
 *
 * @param freq_map
 */
HuffmanTree::HuffmanTree(const std::vector<uint32_t>& freq_map,
                         size_t dictionary_size, size_t symbol_bits)
    : dictionary_size_(dictionary_size), symbol_bits_(symbol_bits) {
    buildTree(freq_map);
}

/**
 * @brief Deserialize a Huffman tree from bitstream (preorder format).
 *
 * Format: 0 = internal node (recurse left, then right)
 *         1 = leaf node (read symbol_bits_ bits for the symbol)
 */
HuffmanTree::HuffmanTree(utils::BitReader& reader, size_t symbol_bits,
                         size_t dictionary_size)
    : dictionary_size_(dictionary_size), symbol_bits_(symbol_bits) {
    auto deserialize = [&](auto& self) -> node* {
        if (!reader.ensureBits(1)) return nullptr;
        uint64_t bit = reader.readBit();
        if (bit == 0) {
            node* left = self(self);
            node* right = self(self);
            if (!left || !right) return nullptr;
            return new node(left, right);
        } else {
            if (!reader.ensureBits(static_cast<uint8_t>(symbol_bits_)))
                return nullptr;
            uint16_t symbol = static_cast<uint16_t>(
                reader.readBits(static_cast<uint8_t>(symbol_bits_)));
            return new node(symbol, 0);
        }
    };
    root_ = deserialize(deserialize);
    tree_size_ = calcSerializedBits(root_);
}

/**
 * @brief Helper function to build Huffman Tree
 *
 * @param freq_map
 */
auto HuffmanTree::buildTree(const std::vector<uint32_t>& freq_map) -> void {
    /* 2. Initialize the Huffman tree */
    std::priority_queue<node*, std::vector<node*>, Compare> pq;

    for (int i = 0; i < dictionary_size_; ++i) {
        if (freq_map[i] > 0) {
            pq.push(new node(static_cast<uint16_t>(i), freq_map[i]));
        }
    }

    /* 3. Construct the Huffman Tree */
    // Handle if size == 1: pair the only node with a dummy
    if (pq.size() == 1) {
        node* temp = pq.top();
        pq.pop();
        pq.push(new node(temp,
                         new node(static_cast<uint16_t>(0),
                                  static_cast<uint32_t>(0))));
    }

    // Handle if size == 0 (all zero frequencies)
    if (pq.empty()) {
        root_ = new node(static_cast<uint16_t>(0), static_cast<uint32_t>(0));
        tree_size_ = calcSerializedBits(root_);
        return;
    }

    while (pq.size() >= 2) {
        // Push the top two node
        node* left = pq.top();
        pq.pop();
        node* right = pq.top();
        pq.pop();

        // Conbine the top two node to a new node
        pq.push(new node(left, right));
    }

    /* 4. Assign the top */
    root_ = pq.top();
    tree_size_ = calcSerializedBits(root_);
}
auto HuffmanTree::calcSerializedBits(const node* n) -> size_t const {
    if (!n) return 0;
    if (n->isLeaf()) return 1 + symbol_bits_;
    return 1 + calcSerializedBits(n->left) + calcSerializedBits(n->right);
}

/**
 * @brief
 *
 * @return std::array<HuffmanCode, 256>
 */
auto HuffmanTree::buildDictionary() const -> std::vector<HuffmanCode> {
    std::vector<HuffmanCode> dict(dictionary_size_);
    generateCodes(root_, 0, 0, dict);
    return dict;
}

auto HuffmanTree::generateCodes(node* n, uint64_t current_code,
                                uint8_t current_length,
                                std::vector<HuffmanCode>& dict) const -> void {
    if (!n) {
        return;
    }

    if (n->isLeaf()) {
        dict[n->symbol] = {current_code, current_length};
        return;
    }

    // LSB = first edge (root), MSB = last edge (leaf)
    // Matches LSB-first bit transmission order
    generateCodes(n->left, current_code, current_length + 1, dict);
    generateCodes(n->right, current_code | (1ULL << current_length),
                  current_length + 1, dict);
}

auto HuffmanTree::serializeTree(utils::BitWriter& writer) const -> void {
    serializeNode(writer, root_);
}

auto HuffmanTree::serializeNode(utils::BitWriter& writer, node* n) const
    -> void {
    if (!n) {
        return;
    }

    if (n->isLeaf()) {
        writer.writeBit(1);
        writer.writeBits(n->symbol, symbol_bits_);
    } else {
        writer.writeBit(0);
        serializeNode(writer, n->left);
        serializeNode(writer, n->right);
    }
}

auto HuffmanTree::getCodeLengths() const -> std::vector<uint8_t> {
    auto dict = buildDictionary();
    std::vector<uint8_t> lengths(dictionary_size_, 0);
    for (size_t i = 0; i < dictionary_size_; ++i) {
        lengths[i] = dict[i].length;
    }
    return lengths;
}

}  // namespace compressor::algorithm

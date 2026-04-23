// Include lib here
#include "HuffmanTree.hpp"

#include <array>
#include <cstdint>
#include <queue>
#include <span>
#include <vector>

#include "BitReader.hpp"
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
HuffmanTree::HuffmanTree(const std::vector<uint32_t>& freq_map) {
    buildTree(freq_map);
}

/**
 * @brief Construct a new Huffman Tree:: Huffman Tree object
 *
 * @param reader
 */
HuffmanTree::HuffmanTree(utils::BitReader& reader) {
    auto buildTreeRecursive = [&reader](auto& self) -> node* {
        if (reader.isEOF()) {
            return nullptr;
        }

        uint8_t bit = reader.readBit();
        if (reader.isEOF()) {
            return nullptr;
        }

        if (bit == 0) {
            node* left = self(self);
            node* right = self(self);
            return new node(left, right);
        } else {
            uint8_t symbol = static_cast<uint8_t>(reader.readBits(8));
            return new node(symbol, 0);
        }
    };

    root_ = buildTreeRecursive(buildTreeRecursive);
}

/**
 * @brief Helper function to build Huffman Tree
 *
 * @param freq_map
 */
auto HuffmanTree::buildTree(const std::vector<uint32_t>& freq_map) -> void {
    /* 2. Initialize the Huffman tree */
    std::priority_queue<node*, std::vector<node*>, Compare> pq;

    for (int i = 0; i < 256; ++i) {
        if (freq_map[i] > 0) {
            pq.push(new node(static_cast<uint8_t>(i), freq_map[i]));
        }
    }

    /* 3. Construct the Huffman Tree */
    // Handle if size == 1
    if (pq.size() == 1) {
        node* temp = pq.top();
        pq.pop();
        root_ = new node(
            temp, new node(static_cast<uint8_t>(0), static_cast<uint32_t>(0)));
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
}

/**
 * @brief
 *
 * @return std::array<HuffmanCode, 256>
 */
auto HuffmanTree::buildDictionary() -> std::array<HuffmanCode, 256> {
    std::array<HuffmanCode, 256> dict{};
    generateCodes(root_, 0, 0, dict);
    return dict;
}

auto HuffmanTree::generateCodes(node* n, uint64_t current_node,
                                uint8_t current_length,
                                std::array<HuffmanCode, 256>& dict) -> void {
    if (!n) {
        return;
    }

    if (n->isLeaf()) {
        dict[n->symbol] = {current_node, current_length};
        return;
    }

    generateCodes(n->left, current_node << 1, current_length + 1, dict);
    generateCodes(n->right, (current_node << 1) | 1, current_length + 1, dict);
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
        writer.writeBits(n->symbol, 8);
    } else {
        writer.writeBit(0);
        serializeNode(writer, n->left);
        serializeNode(writer, n->right);
    }
}

}  // namespace compressor::algorithm

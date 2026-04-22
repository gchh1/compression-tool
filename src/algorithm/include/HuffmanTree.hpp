#pragma once

// Include lib here
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "BitReader.hpp"
#include "BitWriter.hpp"

namespace compressor::algorithm {

/** @brief Node struct */
struct node {
    uint8_t symbol;
    uint32_t frequency;
    node* left = nullptr;
    node* right = nullptr;

    /* Constructor */
    node(uint8_t d, uint32_t f) : symbol(d), frequency(f) {}
    // Construct with left node and right node
    node(node* l, node* r)
        : frequency(l->frequency + r->frequency), left(l), right(r) {}

    /* Return true if the node is leaf */
    bool isLeaf(void) const { return left == nullptr && right == nullptr; }
};

/** @brief Compare class for node */
class Compare {
   public:
    bool operator()(node* a, node* b) { return a->frequency > b->frequency; }
};

/** @brief  */
struct HuffmanCode {
    uint64_t code{0};
    uint8_t length{0};
};

/**
 * @brief Basic Huffman Tree class, recieve a vector<uint8_t> byte stream and
 *        build to a vector<string> code dictionary.
 *        Since that the data stream is byte stream, instead of using
 *        unordered_map, we using a vecotr with capacity of 256.
 *
 */
class HuffmanTree {
   public:
    /** @brief Delete default constructor, copy constructor and copy assian */
    HuffmanTree() = delete;
    HuffmanTree(const HuffmanTree&) = delete;
    HuffmanTree& operator=(const HuffmanTree&) = delete;

    /** @brief Recieve byte stream, and init the Huffman Tree */
    explicit HuffmanTree(const std::vector<uint32_t>& freq_map);
    explicit HuffmanTree(std::span<const uint8_t> symbols);

    /** @brief Build the Huffman Tree with preorder tree code */
    explicit HuffmanTree(utils::BitReader& reader);

    /** @brief Obey RAII (Resources Acqusition is Initialization) */
    ~HuffmanTree() {
        auto destory = [](auto& self, node* n) -> void {
            if (n == nullptr) {
                return;
            }
            self(self, n->left);
            self(self, n->right);
            delete n;
        };
        destory(destory, root_);
    }

    /** @brief Return the dictionary */
    auto buildDictionary(void) -> std::array<HuffmanCode, 256>;

    /** @brief Return the Huffman Tree we build */
    auto serializeTree(utils::BitWriter& writer) const -> void;

    /** @brief Return the root of the Huffman Tree */
    auto getRoot(void) -> node* const { return root_; }

   private:
    /** @brief Build the huffman tree */
    auto buildTree(const std::vector<uint32_t>& freqMap) -> void;

    /** @brief Travel the Huffman Tree by preorder to get the code */
    auto generateCodes(node* n, uint64_t current_code, uint8_t current_length,
                       std::array<HuffmanCode, 256>& dict) -> void;

    /** @brief  */
    auto serializeNode(utils::BitWriter& writer, node* n) const -> void;

    /** @brief root of the Huffman Tree */
    node* root_ = nullptr;
};

}  // namespace compressor::algorithm

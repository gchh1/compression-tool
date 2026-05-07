#pragma once

// Include lib here
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "BitReader.hpp"
#include "BitWriter.hpp"

namespace compressor::algorithm {

/** @brief 256(literal) + 1(EOF) + 29(Length) = 286 */
constexpr size_t DEFLATE_ALPHABET_SIZE = 286;

/** @brief How many bits to store the size */
constexpr uint8_t DEFLATE_SYMBOL_BITS = 9;

/** @brief Node struct */
struct node {
    uint16_t symbol;
    uint32_t frequency;
    node* left = nullptr;
    node* right = nullptr;

    /* Constructor */
    node(uint16_t d, uint32_t f) : symbol(d), frequency(f) {}
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
 * @brief Helpful class to build and manage `HuffmanTree`. As for constructor,
 *        the class can receive `freq_map`, `symbols` and `BitReader`. Then
 *        build a `dictionary`, which is an `array` of size `256`, each element
 * is a `Huffman Code` stand for the `ASCII` index.
 *
 */
class HuffmanTree {
   public:
    /** @brief Delete default constructor, copy constructor and copy assian */
    HuffmanTree() = delete;
    HuffmanTree(const HuffmanTree&) = delete;
    HuffmanTree& operator=(const HuffmanTree&) = delete;

    /** @brief Recieve byte stream, and init the Huffman Tree */
    HuffmanTree(const std::vector<uint32_t>& freq_map, size_t dictionary_size,
                size_t symbol_bits);

    explicit HuffmanTree(std::span<const uint8_t> symbols);

    /** @brief Build the Huffman Tree with preorder tree code */
    // explicit HuffmanTree(utils::BitReader& reader);

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

    /** @brief Build and return the dictionary */
    auto buildDictionary(void) const -> std::vector<HuffmanCode>;

    /** @brief Return the Huffman Tree we build */
    auto serializeTree(utils::BitWriter& writer) const -> void;

    /** @brief Return the root of the Huffman Tree */
    auto getRoot(void) -> node* const { return root_; }

    auto getTreeSize(void) -> size_t const { return tree_size_; }

   private:
    /** @brief Build the huffman tree */
    auto buildTree(const std::vector<uint32_t>& freqMap) -> void;

    /** @brief Travel the Huffman Tree by preorder to get the code */
    auto generateCodes(node* n, uint64_t current_code, uint8_t current_length,
                       std::vector<HuffmanCode>& dict) const -> void;

    /** @brief  */
    auto serializeNode(utils::BitWriter& writer, node* n) const -> void;

    /** @brief  */
    auto calcSerializedBits(const node* n) -> size_t const;

    /** @brief root of the Huffman Tree */
    node* root_{nullptr};

    size_t tree_size_{0};

    /** @brief  */
    size_t dictionary_size_{DEFLATE_ALPHABET_SIZE};
    size_t symbol_bits_{DEFLATE_SYMBOL_BITS};
};

}  // namespace compressor::algorithm

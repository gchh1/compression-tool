#pragma once

// Include lib here
#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <vector>

namespace compressor {
namespace algorithm {
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
    explicit HuffmanTree(const std::vector<uint8_t>& symbols);

    /** @brief Build the Huffman Tree with preorder tree code */
    template <typename BitReader>
    explicit HuffmanTree(BitReader readBit);

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
    std::vector<std::string> encode(void);

    /** @brief Return the Huffman Tree we build */
    std::vector<uint8_t> getTree(void) const { return tree_; }

    /** @brief Return the root of the Huffman Tree */
    node* getRoot(void) const { return root_; }

   private:
    /** @brief Build the huffman tree */
    void buildTree(const std::vector<uint32_t>& freqMap);

    /** @brief Travel the Huffman Tree by preorder to get the code */
    void preorder(node* n, std::string& cur, std::vector<std::string>& res);

    /** @brief root of the Huffman Tree */
    node* root_ = nullptr;

    /** @brief encode the tree by preorder */
    std::vector<uint8_t> tree_;
};

/**
 * @brief The template function must be declared in the .hpp file. Here we
 *        receive a lambda function to work through the bit stream and restore
 * the Huffman Tree
 *
 * @tparam BitReader
 * @param readBit
 */
template <typename BitReader>
HuffmanTree::HuffmanTree(BitReader readBit) {
    auto buildTree = [&readBit](auto& self) -> node* {
        int bit = readBit();

        if (bit == -1) {
            return nullptr;
        }

        // Nonleaf node
        if (bit == 0) {
            node* left = self(self);
            node* right = self(self);
            return new node(left, right);
        }
        // Leaf node
        else {
            // Read the following 8 bits to construct the data
            uint8_t temp = 0;
            for (int i = 0; i < 8; ++i) {
                int bit1 = readBit();
                if (bit1 == -1) {
                    break;
                }
                temp = (temp << 1) | bit1;
            }
            return new node(temp, 0);
        }
    };

    root_ = buildTree(buildTree);
}

}  // namespace algorithm

}  // namespace compressor
#pragma once

// Include lib here
#include <cstddef>
#include <cstdint>
#include <new>
#include <string>
#include <vector>

namespace compressor {
namespace algorithm {
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
    HuffmanTree(const std::vector<uint8_t>& symbols);

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
    std::vector<std::string> encode(void) const;

   private:
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
        bool operator()(node* a, node* b) {
            return a->frequency > b->frequency;
        }
    };

    /** @brief Travel the Huffman Tree by preorder to get the code */
    void preorder(node* n, std::string& cur,
                  std::vector<std::string>& res) const;

    /** @brief root of the Huffman Tree */
    node* root_ = nullptr;
};

}  // namespace algorithm

}  // namespace compressor
// Include lib here
#include "HuffmanTree.hpp"

#include <cstdint>
#include <queue>
#include <string>
#include <vector>

namespace compressor {
namespace algorithm {

/**
 * @brief Construct a new Huffman Tree:: Huffman Tree object
 *
 * @param symbols
 */
HuffmanTree::HuffmanTree(const std::vector<uint8_t>& symbols) {
    if (symbols.empty()) {
        root_ = nullptr;
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
 * @brief Return the encode
 *
 * @return std::vector<std::string>
 */
std::vector<std::string> HuffmanTree::encode(void) {
    // Clear the tree to protect
    tree_.clear();
    std::vector<std::string> dictionary(256, "");

    std::string temp = "";
    preorder(root_, temp, dictionary);

    return dictionary;
}

/**
 * @brief Helper function to build Huffman Tree
 *
 * @param freq_map
 */
void HuffmanTree::buildTree(const std::vector<uint32_t>& freq_map) {
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
        node* temp = new node(left, right);

        pq.push(temp);
    }

    /* 4. Assign the top */
    root_ = pq.top();
}

/**
 * @brief Encode helper function, travel the tree by preorder
 *
 * @param n
 * @param cur
 * @param res
 */
void HuffmanTree::preorder(node* n, std::string& cur,
                           std::vector<std::string>& res) {
    // Return condition #1: n is nullptr
    if (n == nullptr) {
        return;
    }

    // Return condition #2: n reach leaf
    if (n->isLeaf()) {
        res[n->symbol] = cur;

        tree_.push_back('1');
        tree_.push_back(n->symbol);
        return;
    }

    tree_.push_back('0');

    cur.push_back('0');
    preorder(n->left, cur, res);
    cur.pop_back();

    cur.push_back('1');
    preorder(n->right, cur, res);
    cur.pop_back();
}

}  // namespace algorithm

}  // namespace compressor
// Include lib here
#include "Huffman.hpp"

#include <cstdint>
#include <vector>

#include "HuffmanTree.hpp"

namespace compressor {
namespace algorithm {
/**
 * @brief
 *
 * @param input
 * @return std::vector<uint8_t>
 */
std::vector<uint8_t> Huffman::compress(const std::vector<uint8_t> &input) {
    std::vector<uint8_t> result;
    // Return if input is empty
    if (input.empty()) {
        return result;
    }

    /* 1. Push the size of input data into the result */
    // The ahead 4 bytes of result is the size of original data
    uint32_t original_size = static_cast<uint32_t>(input.size());
    result.push_back((original_size >> 24) & 0xFF);
    result.push_back((original_size >> 16) & 0xFF);
    result.push_back((original_size >> 8) & 0xFF);
    result.push_back((original_size) & 0xFF);

    /* 2. Serialize the huffman tree to the head */
    HuffmanTree huffman_tree(input);
    std::vector<std::string> dictionary = huffman_tree.encode();
    // Convert tree represented by byte to bit
    uint8_t bit_buffer = 0;
    uint8_t bit_idx = 0;

    // Helper function
    auto writeBit = [&](uint8_t bit) {
        // Write the bit to the buffer
        bit_buffer = (bit_buffer << 1) | (bit & 1);
        bit_idx++;

        // Flush the buffer when full
        if (bit_idx == 8) {
            result.push_back(bit_buffer);
            bit_buffer = 0;
            bit_idx = 0;
        }
    };

    // Get the tree
    std::vector<uint8_t> tree = huffman_tree.getTree();

    // Main convert
    for (size_t i = 0; i < tree.size(); ++i) {
        if (tree[i] == '0') {
            writeBit(0);
        } else if (tree[i] == '1') {
            writeBit(1);

            // The following 8 bits is the leaf character
            uint8_t temp = tree[++i];
            for (int j = 7; j >= 0; --j) {
                writeBit((temp >> j) & 1);
            }
        }
    }

    /* 3. Compress the input */
    for (const uint8_t &item : input) {
        // Get the Huffman code for each byte
        std::string temp = dictionary[item];

        // Iterate the code and push to result bit by bit
        for (const auto &elem : temp) {
            if (elem == '0') {
                writeBit(0);
            } else if (elem == '1') {
                writeBit(1);
            }
        }
    }

    // Handle the rest
    if (bit_idx > 0) {
        bit_buffer = bit_buffer << (8 - bit_idx);
        result.push_back(bit_buffer);
    }

    return result;
}

/**
 * @brief
 *
 * @param input
 * @return std::vector<uint8_t>
 */
std::vector<uint8_t> Huffman::decompress(const std::vector<uint8_t> &input) {
    std::vector<uint8_t> result;
    // Return if the size of input < 4
    if (input.size() < 4) {
        return result;
    }

    /* 1. Decode the size of original data */
    uint32_t original_size = (static_cast<uint32_t>(input[0]) << 24) |
                             (static_cast<uint32_t>(input[1]) << 16) |
                             (static_cast<uint32_t>(input[2]) << 8) |
                             static_cast<uint32_t>(input[3]);

    /* 2. Decode the Huffman Tree */
    size_t byte_idx = 4;
    uint8_t bit_idx = 0;

    // Lambda helper function to read the input bit by bit
    auto readBit = [&]() -> int {
        // EOF protection
        if (byte_idx >= input.size()) {
            return -1;
        }

        int bit = (input[byte_idx] >> (7 - bit_idx)) & 1;

        bit_idx++;
        if (bit_idx == 8) {
            bit_idx = 0;
            byte_idx++;
        }
        return bit;
    };

    // Restore the Huffman Tree
    HuffmanTree huffman_tree(readBit);

    /* 3. Decode the data by the Tree */
    node *root = huffman_tree.getRoot();
    node *cursor = root;

    while (result.size() < original_size) {
        int bit = readBit();
        if (bit == -1) {
            break;
        }

        // Update the cursor
        cursor = bit == 0 ? cursor->left : cursor->right;

        // Reach leaf
        if (cursor && cursor->isLeaf()) {
            result.push_back(cursor->symbol);
            cursor = root;
        }
    }

    return result;
}

}  // namespace algorithm

}  // namespace compressor

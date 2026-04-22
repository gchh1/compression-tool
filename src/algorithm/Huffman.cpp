// Include lib here
#include "Huffman.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "BitReader.hpp"
#include "BitWriter.hpp"
#include "HuffmanTree.hpp"

namespace compressor::algorithm {

auto Huffman::reset(void) -> void {
    encode_buffer_ = 0;
    encode_buffer_idx_ = 0;
    decode_buffer_ = 0;
    decode_buffer_idx_ = 0;
    decode_state_ = DecodeState::READ_SIZE;
    current_decode_size_ = 0;
}

/**
 * @brief
 *
 * @param input
 * @return std::vector<uint8_t>
 */
auto Huffman::compress(std::span<const uint8_t> read, std::span<uint8_t> write,
                       bool is_last) -> size_t {
    if (read.empty() && !is_last) {
        return 0;
    }

    /* 0. Instantial BitReader and BitWriter*/
    utils::BitWriter writer(write, encode_buffer_, encode_buffer_idx_);

    if (!read.empty()) {
        /* 1. Push the size of input data into the result */
        // The ahead 4 bytes of result is the size of original data
        writer.writeBits(static_cast<uint32_t>(read.size()), 32);

        /* 2. Build and get the huffman tree */
        HuffmanTree huffman_tree(read);
        std::vector<std::string> dictionary = huffman_tree.encode();

        // Get the tree
        std::vector<uint8_t> tree = huffman_tree.getTree();

        /* 3. Serialize the Huffman tree */
        for (size_t i = 0; i < tree.size(); ++i) {
            if (tree[i] == '0') {
                writer.writeBit(0);
            } else if (tree[i] == '1') {
                writer.writeBit(1);

                // The following 8 bits is the leaf character
                writer.writeBits(tree[++i], 8);
            }
        }

        /* 3. Compress the input */
        for (const uint8_t &item : read) {
            // Get the Huffman code for each byte
            const std::string temp = dictionary[item];

            // Iterate the code and push to result bit by bit
            for (const auto &elem : temp) {
                writer.writeBit(elem - '0');
            }
        }
    }

    if (is_last) {
        writer.flush();
        encode_buffer_ = 0;
        encode_buffer_idx_ = 0;
    } else {
        encode_buffer_ = writer.getBuffer();
        encode_buffer_idx_ = writer.getBufferIdx();
    }

    return writer.getBytesWritten();
}

/**
 * @brief
 *
 * @param input
 * @return std::vector<uint8_t>
 */
auto Huffman::decompress(std::span<const uint8_t> read,
                         std::span<uint8_t> write, bool is_last) -> size_t {
    /* 0. Instantialize `BitReader`*/
    utils::BitReader reader(read, decode_buffer_, decode_buffer_idx_);
    size_t bytes_written = 0;

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

}  // namespace compressor::algorithm

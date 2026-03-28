// Include lib here
#include "LZ77.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace compressor {
namespace algorithm {
/**
 * @brief
 *
 * @param input
 * @return std::vector<uint8_t>
 */
std::vector<uint8_t> LZ77::compress(const std::vector<uint8_t> &input) {
    // Result vector that contains compressed data
    std::vector<uint8_t> result;

    /* 1. Return empty vector if input is empty */
    if (input.empty()) {
        return result;
    }

    /* 2. Push the size of input data into the result */
    // The ahead 4 bytes of result is the size of original data
    uint32_t original_size = static_cast<uint32_t>(input.size());
    result.push_back((original_size >> 24) & 0xFF);
    result.push_back((original_size >> 16) & 0xFF);
    result.push_back((original_size >> 8) & 0xFF);
    result.push_back((original_size) & 0xFF);

    /* 3. LZ77 */
    // Initialize the cursor to the head of data
    size_t cursor = 0;

    // Terminate when cursor reach the end of input
    while (cursor < input.size()) {
        uint16_t position = 0;
        uint8_t length = 0;
        // Set the search start position
        size_t search_start = (cursor > DICTIONARY_BUFFER_SIZE_)
                                  ? (cursor - DICTIONARY_BUFFER_SIZE_)
                                  : 0;

        // Search for the longest match
        for (size_t i = search_start; i < cursor; ++i) {
            uint8_t cur_len = 0;

            while (cur_len < LOOKAHEAD_BUFFER_SIZE_ &&
                   cursor + cur_len < input.size() - 1 &&
                   input[i + cur_len] == input[cursor + cur_len]) {
                cur_len++;
            }
            if (cur_len > length) {
                length = cur_len;
                position = cursor - i;
            }
        }

        // Get the next character
        uint8_t next_char = 0;
        if (cursor + length < input.size()) {
            next_char = input[cursor + length];
        }

        // Push the tuple (position, length, next_char)
        result.push_back(position >> 8);
        result.push_back(position & 0xFF);
        result.push_back(length);
        result.push_back(next_char);

        // Update the cursor
        cursor += length + 1;
    }

    return result;
}

/**
 * @brief
 *
 * @param input
 * @return std::vector<uint8_t>
 */
std::vector<uint8_t> LZ77::decompress(const std::vector<uint8_t> &input) {
    std::vector<uint8_t> result;
    // Return if the size of input < 4
    if (input.size() < 4) {
        return result;
    }

    /* 1. Get the size of the original data */
    uint32_t original_size = (static_cast<uint32_t>(input[0]) << 24) |
                             (static_cast<uint32_t>(input[1]) << 16) |
                             (static_cast<uint32_t>(input[2]) << 8) |
                             static_cast<uint32_t>(input[3]);

    /* 2. Restore the data */
    size_t i = 4;
    while (i < input.size() && result.size() < original_size) {
        // Break if i + 3 reach the end
        if (i + 3 >= input.size()) {
            break;
        }

        // Read the tuple
        uint16_t position = (input[i] << 8) | input[i + 1];
        uint8_t length = input[i + 2];
        uint8_t data = input[i + 3];

        i += 4;
        if (length > 0) {
            size_t start = result.size() - position;
            for (int j = 0; j < length; ++j) {
                result.push_back(result[start + j]);
            }
        }
        if (result.size() < original_size) {
            result.push_back(data);
        }
    }

    return result;
}

}  // namespace algorithm
}  // namespace compressor
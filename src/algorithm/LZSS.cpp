// Include lib here

#include "LZSS.hpp"

#include <cstdint>
#include <vector>

namespace compressor {
namespace algorithm {
/**
 * @brief 4 bytes for each character, 1 byte for flag, 12 bits for position, 4
 *        bits for length, 1 byte for char
 *
 * @param input
 * @return std::vector<uint8_t>
 */
std::vector<uint8_t> LZSS::compress(const std::vector<uint8_t> &input) {
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

    /* 3. LZSS */
    uint8_t flag_byte = 0;
    uint8_t flag_byte_idx = 0;
    std::vector<uint8_t> token_buffer;

    // Helper lambda function
    auto flush_token = [&]() {
        result.push_back(flag_byte);
        result.insert(result.end(), token_buffer.begin(), token_buffer.end());
        flag_byte = 0;
        flag_byte_idx = 0;
        token_buffer.clear();
    };

    size_t cursor = 0;
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

            while (cur_len < MAX_MATCH_LENGTH_ &&
                   cursor + cur_len < input.size() &&
                   input[i + cur_len] == input[cursor + cur_len]) {
                cur_len++;
            }
            if (cur_len > length) {
                length = cur_len;
                position = cursor - i;
            }
        }

        // If length < MIN_MATCH_LENGTH_, output (1, char)
        if (length < MIN_MATCH_LENGTH_) {
            flag_byte |= (1 << flag_byte_idx);
            token_buffer.push_back(input[cursor]);
            cursor++;
        }
        // Else, output (0, position, length)
        else {
            // Conbine position and length as a 16 bits token
            uint16_t token = (position << 4) | (length - MIN_MATCH_LENGTH_);

            token_buffer.push_back(token >> 8);
            token_buffer.push_back(token & 0xFF);

            cursor += length;
        }

        flag_byte_idx++;

        // Flush the token to the result vector per 8 byte
        if (flag_byte_idx == 8) {
            flush_token();
        }
    }

    // Flush the rest token
    if (flag_byte_idx != 0) {
        flush_token();
    }

    return result;
}

/**
 * @brief
 *
 * @param input
 * @return std::vector<uint8_t>
 */
std::vector<uint8_t> LZSS::decompress(const std::vector<uint8_t> &input) {
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
        // Get the flag byte
        uint8_t flag_byte = input[i];
        i++;

        // Iterate the following 8 data
        for (uint8_t j = 0; j < 8; ++j) {
            // Break
            if (result.size() >= original_size || i >= input.size()) {
                break;
            }

            // If flag == 0, simply append the character
            if ((flag_byte >> j) & 1) {
                result.push_back(input[i]);
                i++;
            }
            // Else, read the following 2 bytes of the input vector
            else {
                if (i + 1 >= input.size()) {
                    break;
                }
                uint16_t token =
                    (static_cast<uint16_t>(input[i]) << 8) | input[i + 1];
                uint16_t position = token >> 4;
                uint8_t length = (token & 0x0F) + MIN_MATCH_LENGTH_;
                i += 2;

                // Append the reapted character
                size_t start = result.size() - position;
                for (uint8_t k = 0; k < length; ++k) {
                    result.push_back(result[start + k]);
                }
            }
        }
    }

    return result;
}

}  // namespace algorithm
}  // namespace compressor
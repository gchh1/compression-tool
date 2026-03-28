// Include lib here

#include "LZSS.hpp"

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
    }

    return result;
}
}  // namespace algorithm
}  // namespace compressor
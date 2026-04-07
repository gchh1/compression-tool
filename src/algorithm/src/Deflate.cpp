// Include lib here

#include "Deflate.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace compressor {
namespace algorithm {

/**
 * @brief LZSS with hash table optimization
 *
 * @param input
 * @return std::vector<Token>
 */
std::vector<Token> Deflate::compress(const std::vector<uint8_t>& input) {
    std::vector<Token> result;
    // Return if input is null
    if (input.empty()) {
        return result;
    }

    /* 1. Init data srtucture */
    const size_t n = input.size();
    // head[hash_code] = Lastest position of the segment
    std::vector<uint16_t> head(HASH_SIZE, -1);
    // prev[pos] = Previous position of the segment with same hash_code
    std::vector<size_t> prev(n, -1);

    size_t cursor = 0;
    while (cursor < n) {
        size_t position;
        size_t length;

        // Start hash map if n - cursor > MIN_MATCH_LENGTH
        if (n - cursor > MIN_MATCH) {
        }
    }

    return result;
}

}  // namespace algorithm

}  // namespace compressor
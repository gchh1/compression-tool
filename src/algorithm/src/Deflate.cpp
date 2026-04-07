// Include lib here

#include "Deflate.hpp"

#include <algorithm>
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
    std::vector<size_t> head(HASH_SIZE, -1);
    // prev[pos] = Previous position of the segment with same hash_code
    std::vector<size_t> prev(n, -1);

    size_t cursor = 0;
    while (cursor < n) {
        size_t position;
        size_t length;

        // Start hash map if n - cursor >= MIN_MATCH_LENGTH
        if (n - cursor >= MIN_MATCH) {
            uint16_t hash_val = getHash(input, cursor);

            /* 1. Get the head of the hash_val */
            size_t match_pos = head[hash_val];

            /* 2. Store the match_pos to the pre */
            prev[cursor] = match_pos;
            head[hash_val] = cursor;

            /* 3. Find the longest match by prev */
            // Prevent from deep search
            size_t chain_length = MAX_CHAIN_LENGTH;

            while (match_pos != -1 && chain_length-- > 0) {
                // Ensure that the position between the cursor and target is le
                // than WINDOW_SIZE
                size_t distance = cursor - match_pos;
                if (distance > WINDOW_SIZE || distance == 0) {
                    break;
                }

                // Calculate the match length of cursor and match_pos
                size_t curr_match_len = 0;
                size_t max_possible_len = std::min(MAX_MATCH, n - cursor);

                while (curr_match_len < max_possible_len &&
                       input[cursor + curr_match_len] ==
                           input[match_pos + curr_match_len]) {
                    curr_match_len++;
                }

                // Update the length and position
                if (curr_match_len > length) {
                    length = curr_match_len;
                    position = distance;
                    // Break if reach the max limnit
                    if (length == max_possible_len) {
                        break;
                    }
                }

                // Update the match_pos
                match_pos = prev[match_pos];
            }

            /* 4. Handle the token */
            // Token would be (0, length, position)
            if (length >= MIN_MATCH) {
                result.push_back({false, static_cast<uint16_t>(length),
                                  static_cast<uint16_t>(position)});

                /* 5. Store the char between cursor and cursor + length */
                for (size_t i = 0; i < length; ++i) {
                    cursor++;
                    if (cursor + MIN_MATCH <= n) {
                        hash_val = getHash(input, cursor);
                        prev[cursor] = head[hash_val];
                        head[hash_val] = cursor;
                    }
                }
                cursor++;
            }
            // Token would be (1, char)
            else {
                result.push_back({true, input[cursor], 0});
                cursor++;
            }
        }
    }

    return result;
}

/**
 * @brief
 *
 * @param tokens
 * @return std::vector<uint8_t>
 */
std::vector<uint8_t> Deflate::decompress(const std::vector<Token>& tokens) {
    std::vector<uint8_t> result;

    for (const auto& token : tokens) {
        // Meet (1, char)
        if (token.is_literal) {
            result.push_back(token.value);
        }
        // Meet (0, length, position)
        else {
            size_t start_idx = result.size() - token.position;
            for (size_t i = 0; i < token.value; ++i) {
                result.push_back(result[start_idx + i]);
            }
        }
    }

    return result;
}

}  // namespace algorithm

}  // namespace compressor
// Include lib here

#include "LZSS.hpp"

#include <cstdint>
#include <vector>

namespace compressor {
namespace algorithm {

std::vector<uint8_t> LZSS::compress(const std::vector<uint8_t> &input, size_t dictionary_buffer_size, size_t min_match_length, bool use_flag_encoding) {
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
    if (use_flag_encoding) {
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
            size_t search_start = (cursor > dictionary_buffer_size)
                                      ? (cursor - dictionary_buffer_size)
                                      : 0;

            // Search for the longest match
            for (size_t i = search_start; i < cursor; ++i) {
                uint8_t cur_len = 0;

                while (cur_len < (min_match_length + 15) &&
                       cursor + cur_len < input.size() &&
                       input[i + cur_len] == input[cursor + cur_len]) {
                    cur_len++;
                }
                if (cur_len > length) {
                    length = cur_len;
                    position = cursor - i;
                }
            }

            // If length < min_match_length, output (1, char)
            if (length < min_match_length) {
                flag_byte |= (1 << flag_byte_idx);
                token_buffer.push_back(input[cursor]);
                cursor++;
            }
            // Else, output (0, position, length)
            else {
                // Conbine position and length as a 16 bits token
                uint16_t token = (position << 4) | (length - min_match_length);

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
    } else {
        // Offset=0 兜底模式 (No grouping flag)
        size_t cursor = 0;
        while (cursor < input.size()) {
            uint16_t position = 0;
            uint8_t length = 0;
            size_t search_start = (cursor > dictionary_buffer_size) ? (cursor - dictionary_buffer_size) : 0;
            for (size_t i = search_start; i < cursor; ++i) {
                uint8_t cur_len = 0;
                while (cur_len < (min_match_length + 15) &&
                       cursor + cur_len < input.size() &&
                       input[i + cur_len] == input[cursor + cur_len]) {
                    cur_len++;
                }
                if (cur_len > length) {
                    length = cur_len;
                    position = cursor - i;
                }
            }

            if (length < min_match_length) {
                // Determine literal run length
                size_t run_len = 0;
                size_t temp_cursor = cursor;
                std::vector<uint8_t> literals;
                while (temp_cursor < input.size() && run_len < 15) {
                    // Quick check if we should break for a match. For simplicity, just gather literals greedily
                    // until we hit 15, then we will output them. 
                    // Better approach: actually check for matches to break the literal run.
                    // But here we do a simple greedy: check next pos match.
                    uint8_t next_len = 0;
                    size_t next_search_start = (temp_cursor > dictionary_buffer_size) ? (temp_cursor - dictionary_buffer_size) : 0;
                    for (size_t i = next_search_start; i < temp_cursor; ++i) {
                        uint8_t c_len = 0;
                        while (c_len < (min_match_length + 15) && temp_cursor + c_len < input.size() && input[i + c_len] == input[temp_cursor + c_len]) {
                            c_len++;
                        }
                        if (c_len > next_len) next_len = c_len;
                    }
                    if (run_len > 0 && next_len >= min_match_length) {
                        break;
                    }
                    literals.push_back(input[temp_cursor]);
                    run_len++;
                    temp_cursor++;
                }
                // Write literal run
                uint16_t token = (0 << 4) | (run_len & 0x0F);
                result.push_back(token >> 8);
                result.push_back(token & 0xFF);
                for (uint8_t lit : literals) {
                    result.push_back(lit);
                }
                cursor += run_len;
            } else {
                uint16_t token = (position << 4) | (length - min_match_length);
                result.push_back(token >> 8);
                result.push_back(token & 0xFF);
                cursor += length;
            }
        }
    }

    return result;
}

std::vector<uint8_t> LZSS::decompress(const std::vector<uint8_t> &input, size_t min_match_length, bool use_flag_encoding) {
    std::vector<uint8_t> result;
    if (input.size() < 4) {
        return result;
    }

    uint32_t original_size = (static_cast<uint32_t>(input[0]) << 24) |
                             (static_cast<uint32_t>(input[1]) << 16) |
                             (static_cast<uint32_t>(input[2]) << 8) |
                             static_cast<uint32_t>(input[3]);

    size_t i = 4;
    
    if (use_flag_encoding) {
        while (i < input.size() && result.size() < original_size) {
            uint8_t flag_byte = input[i];
            i++;
            for (uint8_t j = 0; j < 8; ++j) {
                if (result.size() >= original_size || i >= input.size()) {
                    break;
                }
                if ((flag_byte >> j) & 1) {
                    result.push_back(input[i]);
                    i++;
                } else {
                    if (i + 1 >= input.size()) break;
                    uint16_t token = (static_cast<uint16_t>(input[i]) << 8) | input[i + 1];
                    uint16_t position = token >> 4;
                    uint8_t length = (token & 0x0F) + min_match_length;
                    i += 2;
                    size_t start = result.size() - position;
                    for (uint8_t k = 0; k < length; ++k) {
                        result.push_back(result[start + k]);
                    }
                }
            }
        }
    } else {
        while (i + 1 < input.size() && result.size() < original_size) {
            uint16_t token = (static_cast<uint16_t>(input[i]) << 8) | input[i + 1];
            i += 2;
            uint16_t position = token >> 4;
            uint8_t length_val = token & 0x0F;
            
            if (position == 0) {
                for (uint8_t k = 0; k < length_val; ++k) {
                    if (i >= input.size()) break;
                    result.push_back(input[i]);
                    i++;
                }
            } else {
                uint8_t length = length_val + min_match_length;
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
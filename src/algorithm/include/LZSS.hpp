#pragma once

// Include lib here
#include <cstdint>
#include <vector>

namespace compressor {
namespace algorithm {

/**
 * @brief Optimazed LZ77. Rather than ouput the tuple (position, length, char),
 *        ouput one of the following two formats (0, position, length) or (1,
 *        char)
 *
 */
class LZSS {
   public:
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input);
    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& input);

   private:
    static constexpr uint16_t DICTIONARY_BUFFER_SIZE_ = 4095;  // 12 bits
    static constexpr uint8_t MIN_MATCH_LENGTH_ = 3;            // 4 bits,
    static constexpr uint8_t MAX_MATCH_LENGTH_ = 18;  // MIN_MATCH_LENGTH + 15
};

}  // namespace algorithm

}  // namespace compressor
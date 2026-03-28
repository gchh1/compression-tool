#pragma once

// Include lib here
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
    static constexpr uint16_t DICTIONARY_BUFFER_SIZE_ = 65535;  // 2 bytes
    static constexpr uint8_t LOOKAHEAD_BUFFER_SIZE_ = 255;      // 1 byte
};

}  // namespace algorithm

}  // namespace compressor
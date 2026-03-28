#pragma once

// Inlcude lib here

#include <sys/types.h>

#include <cstdint>
#include <vector>
namespace compressor {
namespace algorithm {

/**
 * @brief basic LZ77 algorithm
 *
 */
class LZ77 {
   public:
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input);
    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& input);

   private:
    static constexpr uint16_t DICTIONARY_BUFFER_SIZE_ = 65535;  // 2 bytes
    static constexpr uint8_t LOOKAHEAD_BUFFER_SIZE_ = 255;      // 1 byte
};

}  // namespace algorithm

}  // namespace compressor
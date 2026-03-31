#pragma once

// Include lib here

#include <sys/types.h>
#include <cstdint>
#include <vector>
namespace compressor {
namespace algorithm {
    /**
     * @brief Huffman compress algorithm
     * 
     */
class Huffman {
   public:
    static std::vector<uint8_t> compress(const std::vector<uint8_t>& input);
    static std::vector<uint8_t> decompress(const std::vector<uint8_t>& input);
};

}  // namespace algorithm

}  // namespace compressor
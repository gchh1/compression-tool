#pragma once

// Include lib here
#include <cstdint>
#include <vector>

#include "IAlgorithm.hpp"

namespace compressor {
namespace algorithm {

class Deflate {
   public:
    static std::vector<Token> compress(const std::vector<uint8_t>& input);
    static std::vector<uint8_t> decompress(const std::vector<Token>& tokens);

   private:
    static constexpr size_t WINDOW_SIZE = 32768;     // 32KB
    static constexpr size_t MIN_MATCH = 3;           // Minimum match length
    static constexpr size_t MAX_MATCH = 258;         // Maximum match length
    static constexpr size_t HASH_SIZE = 32768;       //
    static constexpr size_t MAX_CHAIN_LENGTH = 256;  // Prevent deep search

    /** @brief Return the hash code */
    static inline uint16_t getHash(const std::vector<uint8_t>& data,
                                   size_t pos) {
        return ((data[pos] << 10) ^ (data[pos + 1] << 5) ^ data[pos + 2]) &
               (HASH_SIZE - 1);
    }
};

}  // namespace algorithm

}  // namespace compressor
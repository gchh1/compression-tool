#pragma once

// Include lib here
#include <cstdint>
#include <vector>

#include "IAlgorithm.hpp"

namespace compressor {
namespace algorithm {

// TODO replace the slide window with hash table
class Deflate {
   public:
    static std::vector<Token> compress(const std::vector<uint8_t>& input);

   private:
    static constexpr size_t WINDOW_SIZE = 32768;  // 32KB
    static constexpr size_t MIN_MATCH = 3;        // Minimum match length
    static constexpr size_t MAX_MATCH = 258;      // Maximum match length
    static constexpr size_t HASH_SIZE = 32768;    // 哈希表大小 (2^15)
    static constexpr size_t MAX_CHAIN_LENGTH =
        256;  // 防死锁：限制单桶最大搜索深度

    /** @brief Return the hash code */
    static inline uint16_t getHash(const std::vector<uint8_t>& data,
                                   size_t pos) {
        return ((data[pos] << 10) ^ (data[pos + 1] << 5) ^ data[pos + 2]) &
               (HASH_SIZE - 1);
    }
};

}  // namespace algorithm

}  // namespace compressor
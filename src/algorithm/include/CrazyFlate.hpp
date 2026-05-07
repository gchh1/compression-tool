#pragma once

#include <cstdint>
#include <vector>

namespace compressor {
namespace algorithm {

class CrazyFlate {
   public:
    static std::vector<uint8_t> compress(
        const std::vector<uint8_t>& input,
        size_t search_size = 4096,
        size_t lookahead_size = 258,
        size_t min_match = 3,
        size_t dp_depth = 6,
        size_t max_chain_length = 128);

    static std::vector<uint8_t> decompress(
        const std::vector<uint8_t>& input);

   private:
    static constexpr size_t MAX_SEARCH_SIZE_ = 65536;
    static constexpr size_t MAX_LOOKAHEAD_SIZE_ = 258;
    static constexpr size_t HASH_SIZE_ = 65536;
    static constexpr uint32_t NULL_PTR_ = UINT32_MAX;

    static size_t calcBits(size_t max_val);
    static uint16_t getHash(const std::vector<uint8_t>& data, size_t pos);
};

}  // namespace algorithm
}  // namespace compressor

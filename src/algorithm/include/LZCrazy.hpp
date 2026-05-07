#pragma once

#include <cstdint>
#include <vector>

namespace compressor {
namespace algorithm {

class LZCrazy {
   public:
    static std::vector<uint8_t> compress(
        const std::vector<uint8_t>& input,
        size_t search_size = 4096,
        size_t lookahead_size = 18,
        size_t min_match = 3);

    static std::vector<uint8_t> decompress(
        const std::vector<uint8_t>& input);

   private:
    static constexpr size_t MAX_SEARCH_SIZE_ = 65536;
    static constexpr size_t MAX_LOOKAHEAD_SIZE_ = 258;

    static size_t calcBits(size_t max_val);
};

}  // namespace algorithm
}  // namespace compressor

#include "Delta.hpp"

#include <cstdint>
#include <vector>

namespace compressor {
namespace algorithm {
std::vector<uint8_t> Delta::encode(const std::vector<uint8_t> &raw_pixels,
                                   int quality) {
    std::vector<uint8_t> result;

    int shift = quality < 100 ? (100 - quality) / 14 : 0;

    uint8_t prev = 0;
    for (auto pixel : raw_pixels) {
        pixel = (pixel >> shift) << shift;
        result.push_back(pixel - prev);
        prev = pixel;
    }

    return result;
}

std::vector<uint8_t> Delta::decode(const std::vector<uint8_t> &pixels) {
    std::vector<uint8_t> result;

    uint8_t prev = 0;
    for (auto pixel : pixels) {
        result.push_back(pixel + prev);
        prev = pixel;
    }
    return result;
}

}  // namespace algorithm

}  // namespace compressor
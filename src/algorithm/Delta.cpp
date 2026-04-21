#include "Delta.hpp"

#include <sys/types.h>

#include <cstdint>
#include <span>
#include <vector>

namespace compressor {
namespace algorithm {
auto Delta::encode(std::span<const uint8_t> read, std::span<uint8_t> write,
                   int quality) -> void {
    int shift = quality < 100 ? (100 - quality) / 14 : 0;

    uint8_t prev = 0;
    for (auto pixel : read) {
        pixel = (pixel >> shift) << shift;
        write.push_back(pixel - prev);
        prev = pixel;
    }

    return result;
}

auto Delta::decode(std::span<const uint8_t> read, std::span<uint8_t> write)
    -> void {
    uint8_t prev = 0;
    for (auto pixel : pixels) {
        pixels.push_back(pixel + prev);
        prev = pixel;
    }
}

}  // namespace algorithm

}  // namespace compressor
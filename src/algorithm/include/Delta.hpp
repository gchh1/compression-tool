#pragma once

#include <sys/types.h>

#include <cstdint>
#include <vector>
namespace compressor {
namespace algorithm {
class Delta {
   public:
    static std::vector<uint8_t> encode(const std::vector<uint8_t>& raw_pixels,
                                       int quality);
    static std::vector<uint8_t> decode(const std::vector<uint8_t>& pixels);
};

}  // namespace algorithm
}  // namespace compressor
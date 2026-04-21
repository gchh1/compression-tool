#pragma once

#include <sys/types.h>

#include <cstdint>
#include <span>
namespace compressor {
namespace algorithm {
class Delta {
   public:
    static auto encode(std::span<const uint8_t> read, std::span<uint8_t> write,
                       int quality) -> void;

    static auto decode(std::span<const uint8_t> read, std::span<uint8_t> write)
        -> void;
};

}  // namespace algorithm
}  // namespace compressor
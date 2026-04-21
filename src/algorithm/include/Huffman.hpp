#pragma once

// Include lib here

#include <sys/types.h>

#include <cstdint>
#include <span>

namespace compressor {
namespace algorithm {
/**
 * @brief Huffman compress algorithm
 *
 */
class Huffman {
   public:
    static auto compress(std::span<const uint8_t> read,
                         std::span<uint8_t> write) -> void;

    static auto decompress(std::span<const uint8_t> read,
                           std::span<uint8_t> write) -> void;
};

}  // namespace algorithm

}  // namespace compressor
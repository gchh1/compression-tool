#include "Delta.hpp"

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace compressor {
namespace algorithm {

Delta::Delta(int quality) { shift_ = quality < 100 ? (100 - quality) / 14 : 0; }

/**
 * @brief
 *
 * @return auto
 */
auto Delta::reset() -> void { prev_ = 0; }

/**
 * @brief
 *
 * @param read
 * @param write
 */
auto Delta::encode(std::span<const uint8_t> read, std::span<uint8_t> write)
    -> void {
    for (size_t i = 0; i < read.size(); ++i) {
        uint8_t pixel = (read[i] >> shift_) << shift_;
        write[i] = pixel - prev_;
        prev_ = pixel;
    }
}

/**
 * @brief
 *
 * @param read
 * @param write
 */
auto Delta::decode(std::span<const uint8_t> read, std::span<uint8_t> write)
    -> void {
    for (size_t i = 0; i < read.size(); ++i) {
        uint8_t pixel = read[i] + prev_;
        write[i] = pixel;
        prev_ = pixel;
    }
}

}  // namespace algorithm

}  // namespace compressor
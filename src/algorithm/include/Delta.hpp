#pragma once

#include <sys/types.h>

#include <cstdint>
#include <span>
namespace compressor {
namespace algorithm {
class Delta {
   public:
    explicit Delta(int quality = 100);

    ~Delta() = default;

    auto encode(std::span<const uint8_t> read, std::span<uint8_t> write)
        -> void;

    auto decode(std::span<const uint8_t> read, std::span<uint8_t> write)
        -> void;

    auto reset() -> void;

   private:
    /** @brief Storing shift_ so that we don't need to calculate it every time
     * we call encode */
    int shift_{0};

    /** @brief Prev_ */
    uint8_t prev_{0};
};

}  // namespace algorithm
}  // namespace compressor
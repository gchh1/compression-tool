#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "IAlgorithm.hpp"

namespace compressor::algorithm {

enum class ImageFormat { JPEG, PNG };

class ImageCompressor : public IAlgorithm {
   public:
    ImageCompressor(ImageFormat format, int quality = 85,
                    int max_width = 0, int max_height = 0)
        : format_(format), quality_(quality),
          max_width_(max_width), max_height_(max_height) {}

    auto process(std::span<const uint8_t> read, std::span<uint8_t> write,
                 bool is_last_chunk) -> AlgorithmStatus override;

    auto reset() -> void override;

   private:
    ImageFormat format_;
    int quality_;
    int max_width_;
    int max_height_;

    std::vector<uint8_t> accumulator_;
    std::vector<uint8_t> output_buffer_;
    size_t output_pos_{0};
    bool finished_{false};

    auto copyOutput(std::span<uint8_t> write) -> size_t;
};

}  // namespace compressor::algorithm

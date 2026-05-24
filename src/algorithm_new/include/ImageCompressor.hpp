#pragma once

#include <cstdint>
#include <vector>

namespace compressor::algorithm {

enum class ImageFormat { JPEG, PNG };

auto image_compress(const std::vector<uint8_t>& data,
                    ImageFormat format = ImageFormat::JPEG,
                    int quality = 85) -> std::vector<uint8_t>;

auto image_decompress(const std::vector<uint8_t>& data) -> std::vector<uint8_t>;

}  // namespace compressor::algorithm
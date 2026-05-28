#pragma once

#include <cstdint>
#include <vector>

namespace compressor::algorithm {

// ── Public API ──

auto openh264_compress(const std::vector<uint8_t>& raw_data,
                       int quality = 26) -> std::vector<uint8_t>;

auto openh264_decompress(const std::vector<uint8_t>& h264_data) -> std::vector<uint8_t>;

}  // namespace compressor::algorithm
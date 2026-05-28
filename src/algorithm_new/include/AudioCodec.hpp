#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace compressor::algorithm {

enum class AudioFormat { FLAC };

struct WavInfo {
    int sample_rate = 0;
    int num_channels = 0;
    int bits_per_sample = 16;
    size_t data_offset = 0;
    size_t data_size = 0;
};

auto parse_wav(const std::vector<uint8_t>& data) -> WavInfo;
auto build_wav(const std::vector<int16_t>& samples, int sample_rate,
               int num_channels, int bits_per_sample) -> std::vector<uint8_t>;

auto audio_compress(const std::vector<uint8_t>& wav_data,
                    AudioFormat format = AudioFormat::FLAC,
                    int quality = 5) -> std::vector<uint8_t>;

auto audio_decompress(const std::vector<uint8_t>& data) -> std::vector<uint8_t>;

namespace flac {

auto encode(const std::vector<int16_t>& samples, int sample_rate,
            int num_channels, int bits_per_sample,
            int compression_level = 5) -> std::vector<uint8_t>;

auto decode(const std::vector<uint8_t>& flac_data) -> std::vector<uint8_t>;

}  // namespace flac

}  // namespace compressor::algorithm
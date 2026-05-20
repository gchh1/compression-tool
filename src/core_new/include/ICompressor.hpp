#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace compressor::core {

struct CompressorResult {
    std::vector<uint8_t> data;
    size_t original_size{0};
    size_t compressed_size{0};
    double compression_ratio{0.0};
    double time_ms{0.0};
    bool success{true};
    std::string error_message;
};

class ICompressor {
public:
    virtual ~ICompressor() = default;

    virtual CompressorResult compress(std::vector<uint8_t> original_data) = 0;
    virtual CompressorResult decompress(std::vector<uint8_t> compressed_data) = 0;
    virtual std::string get_algorithm_name() = 0;
};

}  // namespace compressor::core

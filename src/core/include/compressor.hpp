#pragma once

// Head lib here
#include <cstdint>
#include <string>
#include <vector>

namespace compressor {
namespace core {

struct CompressorResult {
    // Represent data stream by byte
    std::vector<uint8_t> data;

    size_t original_size;
    size_t compressed_size;
    double compression_ratio;

    // Time in ms stores the algorithm time cost
    double time_ms;
};

// Compressor interface
class ICompressor {
   public:
    virtual ~ICompressor() = default;

    virtual CompressorResult compress(
        const std::vector<uint8_t>& original_data) = 0;
    virtual CompressorResult decompress(
        const std::vector<uint8_t>& compressed_data) = 0;

    virtual auto get_algorithm_name(void) -> std::string = 0;
};

}  // namespace core
}  // namespace compressor
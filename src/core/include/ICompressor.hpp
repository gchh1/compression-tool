#pragma once

// Head lib here
#include <cstdint>
#include <string>
#include <vector>

namespace compressor {
namespace core {

struct CompressorResult {
    std::vector<uint8_t> data;

    size_t original_size;
    size_t compressed_size;
    double compression_ratio;

    double time_ms;

    bool success{true};
    std::string error_message;
};

enum class CompressorAlgorithm { Deflate, LZSS, LZMINE, LZCRAZY, CRAZYFLATE };

// Compressor interface
class ICompressor {
   public:
    virtual ~ICompressor() = default;

    virtual auto compress(std::vector<uint8_t> original_data)
        -> CompressorResult = 0;

    virtual auto decompress(std::vector<uint8_t> compressed_data)
        -> CompressorResult = 0;

    inline virtual auto get_algorithm_name(void) -> std::string = 0;
};

}  // namespace core
}  // namespace compressor
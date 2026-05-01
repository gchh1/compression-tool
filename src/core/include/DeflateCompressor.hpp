#pragma once

#include <cstdint>
#include <vector>

#include "ICompressor.hpp"

namespace compressor {
namespace core {

class DeflateCompressor : public ICompressor {
public:
    auto compress(std::vector<uint8_t> original_data)
        -> CompressorResult override;

    auto decompress(std::vector<uint8_t> compressed_data)
        -> CompressorResult override;

    inline std::string get_algorithm_name(void) override {
        return "Deflate (LZ77 + Huffman)";
    }
};

} // namespace core
} // namespace compressor

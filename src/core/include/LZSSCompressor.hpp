#pragma once

#include <cstdint>
#include <vector>

#include "ICompressor.hpp"

namespace compressor {
namespace core {
class LZSSCompressor : public ICompressor {
   public:
    auto compress(std::vector<uint8_t> data) -> CompressorResult override;

    auto decompress(std::vector<uint8_t> data) -> CompressorResult override;

    inline auto get_algorithm_name(void) -> std::string override {
        return "LZSS";
    }
};

}  // namespace core

}  // namespace compressor
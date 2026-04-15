#pragma once

#include <cstdint>
#include <vector>

#include "compressor.hpp"

namespace compressor {
namespace core {
class LZSSCompressor : public ICompressor {
   public:
    CompressorResult compress(const std::vector<uint8_t>& data) override;
    CompressorResult decompress(const std::vector<uint8_t>& data) override;
    std::string get_algorithm_name(void) override { return "LZSS"; }
};

}  // namespace core

}  // namespace compressor
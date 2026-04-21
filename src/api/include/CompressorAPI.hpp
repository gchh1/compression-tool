#pragma once
#include <cstdint>
#include <vector>

#include "Archiver.hpp"
#include "ICompressor.hpp"

namespace compressor {
namespace api {

using core::CompressorResult;
using core::File;

class CompressorAPI {
   public:
    static auto compress(const std::vector<File>& files, int image_quality)
        -> CompressorResult;

    static auto decompress(const std::vector<uint8_t>& compressed_data)
        -> CompressorResult;
};

}  // namespace api
}  // namespace compressor
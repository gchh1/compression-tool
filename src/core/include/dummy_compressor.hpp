#pragma once

// Include lib here
#include <sys/types.h>

#include <cstdint>
#include <vector>

#include "compressor.hpp"

//
namespace compressor {
namespace core {
class DummyCompressor : public ICompressor {
   public:
    auto compress(const std::vector<uint8_t>& original_data)
        -> CompressResult override;
    auto decompress(const std::vector<uint8_t>& compressed_data)
        -> CompressResult override;
    auto get_algorithm_name(void) -> std::string override;
};
}  // namespace core
}  // namespace compressor
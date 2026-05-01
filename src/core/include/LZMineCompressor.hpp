#pragma once

#include <cstdint>
#include <vector>

#include "ICompressor.hpp"
#include "LZMine.hpp"

namespace compressor {
namespace core {

class LZMineCompressor : public ICompressor {
public:
    auto compress(std::vector<uint8_t> data) -> CompressorResult override;

    auto decompress(std::vector<uint8_t> data) -> CompressorResult override;

    inline auto get_algorithm_name(void) -> std::string override {
        return "LZMine (KMP+DP+LiteralRun)";
    }

private:
    algorithm::LZMine lzmine_{2, 2};
};

} // namespace core
} // namespace compressor

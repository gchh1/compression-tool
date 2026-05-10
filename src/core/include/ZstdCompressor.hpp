#pragma once

#include <cstdint>
#include <vector>

#include "ICompressor.hpp"

namespace compressor {
namespace core {

class ZstdCompressor : public ICompressor {
public:
    ZstdCompressor()
        : compression_level_(3) {}

    auto compress(std::vector<uint8_t> original_data)
        -> CompressorResult override;

    auto decompress(std::vector<uint8_t> compressed_data)
        -> CompressorResult override;

    inline std::string get_algorithm_name(void) override {
        return "Zstd";
    }

    void set_compression_level(int v) { compression_level_ = v; }
    int get_compression_level() const { return compression_level_; }

private:
    int compression_level_;
};

}
}

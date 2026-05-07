#pragma once

#include <cstdint>
#include <vector>

#include "ICompressor.hpp"

namespace compressor {
namespace core {

class GzipCompressor : public ICompressor {
public:
    GzipCompressor()
        : compression_level_(6) {}

    auto compress(std::vector<uint8_t> original_data)
        -> CompressorResult override;

    auto decompress(std::vector<uint8_t> compressed_data)
        -> CompressorResult override;

    inline std::string get_algorithm_name(void) override {
        return "Gzip (zlib standard)";
    }

    void set_compression_level(size_t v) { compression_level_ = v; }
    size_t get_compression_level() const { return compression_level_; }

private:
    size_t compression_level_;
};

} // namespace core
} // namespace compressor

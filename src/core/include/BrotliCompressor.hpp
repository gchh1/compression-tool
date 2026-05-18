#pragma once

#include <cstdint>
#include <vector>

#include "ICompressor.hpp"

namespace compressor {
namespace core {

class BrotliCompressor : public ICompressor {
public:
    BrotliCompressor()
        : window_size_(65536), min_match_(3), max_chain_length_(256) {}

    auto compress(std::vector<uint8_t> original_data)
        -> CompressorResult override;

    auto decompress(std::vector<uint8_t> compressed_data)
        -> CompressorResult override;

    inline std::string get_algorithm_name(void) override {
        return "Brotli";
    }

    void set_window_size(size_t v) { window_size_ = v; }
    size_t get_window_size() const { return window_size_; }

    void set_min_match(size_t v) { min_match_ = v; }
    size_t get_min_match() const { return min_match_; }

    void set_max_chain_length(size_t v) { max_chain_length_ = v; }
    size_t get_max_chain_length() const { return max_chain_length_; }

private:
    size_t window_size_;
    size_t min_match_;
    size_t max_chain_length_;
};

}
}
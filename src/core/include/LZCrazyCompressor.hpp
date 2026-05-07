#pragma once

#include <cstdint>
#include <vector>

#include "ICompressor.hpp"

namespace compressor {
namespace core {

class LZCrazyCompressor : public ICompressor {
   public:
    LZCrazyCompressor()
        : search_size_(4096),
          lookahead_size_(18),
          min_match_(3) {}

    auto compress(std::vector<uint8_t> data) -> CompressorResult override;
    auto decompress(std::vector<uint8_t> data) -> CompressorResult override;

    inline auto get_algorithm_name(void) -> std::string override {
        return "LZCrazy";
    }

    void set_search_size(size_t v) { search_size_ = v; }
    size_t get_search_size() const { return search_size_; }

    void set_lookahead_size(size_t v) { lookahead_size_ = v; }
    size_t get_lookahead_size() const { return lookahead_size_; }

    void set_min_match(size_t v) { min_match_ = v; }
    size_t get_min_match() const { return min_match_; }

   private:
    size_t search_size_;
    size_t lookahead_size_;
    size_t min_match_;
};

}  // namespace core
}  // namespace compressor

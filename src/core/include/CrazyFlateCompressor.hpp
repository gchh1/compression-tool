#pragma once

#include <cstdint>
#include <vector>

#include "ICompressor.hpp"

namespace compressor {
namespace core {

class CrazyFlateCompressor : public ICompressor {
   public:
    CrazyFlateCompressor()
        : search_size_(4096),
          lookahead_size_(258),
          min_match_(3),
          dp_depth_(6),
          max_chain_length_(128) {}

    auto compress(std::vector<uint8_t> data) -> CompressorResult override;
    auto decompress(std::vector<uint8_t> data) -> CompressorResult override;

    inline auto get_algorithm_name(void) -> std::string override {
        return "CrazyFlate";
    }

    void set_search_size(size_t v) { search_size_ = v; }
    size_t get_search_size() const { return search_size_; }

    void set_lookahead_size(size_t v) { lookahead_size_ = v; }
    size_t get_lookahead_size() const { return lookahead_size_; }

    void set_min_match(size_t v) { min_match_ = v; }
    size_t get_min_match() const { return min_match_; }

    void set_dp_depth(size_t v) { dp_depth_ = v; }
    size_t get_dp_depth() const { return dp_depth_; }

    void set_max_chain_length(size_t v) { max_chain_length_ = v; }
    size_t get_max_chain_length() const { return max_chain_length_; }

   private:
    size_t search_size_;
    size_t lookahead_size_;
    size_t min_match_;
    size_t dp_depth_;
    size_t max_chain_length_;
};

}  // namespace core
}  // namespace compressor

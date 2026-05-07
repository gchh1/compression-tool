#pragma once

#include <cstdint>
#include <vector>

#include "ICompressor.hpp"

namespace compressor {
namespace core {

class MyFlateCompressor : public ICompressor {
public:
    MyFlateCompressor()
        : search_size_(4096),
          lookahead_size_(256),
          min_match_(4),
          max_chain_length_(256),
          dp_sub_match_max_(6) {}

    auto compress(std::vector<uint8_t> original_data)
        -> CompressorResult override;

    auto decompress(std::vector<uint8_t> compressed_data)
        -> CompressorResult override;

    inline std::string get_algorithm_name(void) override {
        return "MyFlate (LZMine KMP + Huffman)";
    }

    void set_search_size(size_t v) { search_size_ = v; }
    size_t get_search_size() const { return search_size_; }

    void set_lookahead_size(size_t v) { lookahead_size_ = v; }
    size_t get_lookahead_size() const { return lookahead_size_; }

    void set_min_match(size_t v) { min_match_ = v; }
    size_t get_min_match() const { return min_match_; }

    void set_max_chain_length(size_t v) { max_chain_length_ = v; }
    size_t get_max_chain_length() const { return max_chain_length_; }

    void set_dp_depth(size_t v) { dp_sub_match_max_ = v; }
    size_t get_dp_depth() const { return dp_sub_match_max_; }

    void set_dp_sub_match_max(size_t v) { dp_sub_match_max_ = v; }
    size_t get_dp_sub_match_max() const { return dp_sub_match_max_; }

private:
    size_t search_size_;
    size_t lookahead_size_;
    size_t min_match_;
    size_t max_chain_length_;
    size_t dp_sub_match_max_;
};

}  // namespace core
}  // namespace compressor

#pragma once

#include <cstdint>
#include <vector>

#include "ICompressor.hpp"
#include "LZDP.hpp"

namespace compressor {
namespace core {

class LZDPCompressor : public ICompressor {
public:
    LZDPCompressor()
        : search_size_(4096),
          lookahead_size_(256),
          dp_range_(3) {}

    auto compress(std::vector<uint8_t> data) -> CompressorResult override;

    auto decompress(std::vector<uint8_t> data) -> CompressorResult override;

    inline auto get_algorithm_name(void) -> std::string override {
        return "LZDP (KMP+DP+BitPack)";
    }

    void set_search_size(size_t v) { search_size_ = v; }
    size_t get_search_size() const { return search_size_; }

    void set_lookahead_size(size_t v) { lookahead_size_ = v; }
    size_t get_lookahead_size() const { return lookahead_size_; }

    void set_min_match(size_t v) { min_match_ = v; }
    size_t get_min_match() const { return min_match_; }

    [[deprecated("use set_dp_top instead")]] void set_dp_depth(size_t v) { dp_range_ = v; }
    [[deprecated("use set_dp_top instead")]] size_t get_dp_depth() const { return dp_range_; }
    void set_dp_top(size_t v) { dp_range_ = v; }
    size_t get_dp_top() const { return dp_range_; }

    [[deprecated("use set_dp_top instead")]] void set_dp_range(size_t v) { dp_range_ = v; }
    [[deprecated("use set_dp_top instead")]] size_t get_dp_range() const { return dp_range_; }

    void set_use_flag_encoding(bool v) { use_flag_encoding_ = v; }
    bool get_use_flag_encoding() const { return use_flag_encoding_; }

    void set_match_engine(int v) { match_engine_ = v; }
    int get_match_engine() const { return match_engine_; }

    algorithm::LZDP::DPVisualization get_dp_visualization(
        const std::vector<uint8_t>& data, size_t range = 0);

private:
    size_t search_size_;
    size_t lookahead_size_;
    size_t min_match_{0};
    size_t dp_range_;
    bool use_flag_encoding_{false};
    int match_engine_{0};
};

} // namespace core
} // namespace compressor
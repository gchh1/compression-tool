#pragma once

#include <cstdint>
#include <vector>

#include "ICompressor.hpp"
#include "LZMine.hpp"

namespace compressor {
namespace core {

class LZMineCompressor : public ICompressor {
public:
    LZMineCompressor()
        : search_size_(4096),
          lookahead_size_(256),
          dp_range_(3) {}

    auto compress(std::vector<uint8_t> data) -> CompressorResult override;

    auto decompress(std::vector<uint8_t> data) -> CompressorResult override;

    inline auto get_algorithm_name(void) -> std::string override {
        return "LZMine (KMP+DP+LiteralRun)";
    }

    void set_search_size(size_t v) { search_size_ = v; }
    size_t get_search_size() const { return search_size_; }

    void set_lookahead_size(size_t v) { lookahead_size_ = v; }
    size_t get_lookahead_size() const { return lookahead_size_; }

    void set_dp_depth(size_t v) { dp_range_ = v; }
    size_t get_dp_depth() const { return dp_range_; }

    void set_dp_range(size_t v) { dp_range_ = v; }
    size_t get_dp_range() const { return dp_range_; }

    algorithm::LZMine::DPVisualization get_dp_visualization(
        const std::vector<uint8_t>& data, size_t range = 0);

private:
    size_t search_size_;
    size_t lookahead_size_;
    size_t dp_range_;
};

} // namespace core
} // namespace compressor

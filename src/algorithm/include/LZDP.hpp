#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "KMPMatcher.hpp"

namespace compressor {
namespace algorithm {

class LZDP {
public:
    size_t get_min_match() const {
        if (min_match_ == 0) {
            return (offset_bits_ + length_bits_) / 8 + 1;
        }
        return min_match_;
    }

    struct Triple {
        size_t offset;
        size_t length;
        uint8_t literal;
        Triple(size_t a, size_t b, uint8_t c)
            : offset(a), length(b), literal(c) {}
        Triple() : offset(0), length(0), literal(0) {}
    };

    struct DPCandidate {
        size_t offset;
        size_t length;
        uint8_t literal;
        bool is_chosen;
    };

    /// Per-byte visualization / array slot; same semantics as LzStyleDpCell
    /// plus explicit index and reachability (choice == Triple(offset,length,literal)).
    struct DPState {
        size_t position;
        bool reachable;
        size_t token_count;
        size_t predecessor;
        Triple choice;
    };

    struct DPStep {
        size_t position;
        std::vector<DPCandidate> candidates;
        size_t best_token_count;
    };

    struct DPVisualization {
        std::vector<DPStep> steps;
        std::vector<DPState> dp_array;
        std::vector<Triple> optimal_path;
        size_t input_length;
        size_t search_size;
        size_t lookahead_size;
    };

private:
    size_t offset_bits_;
    size_t length_bits_;
    size_t max_search_size_;
    size_t max_look_size_;
    bool use_flag_encoding_;
    int match_engine_{0}; // 0 = KMP, 1 = HashChain
    size_t min_match_{0};

    static size_t calcBitWidth(size_t max_val);

public:
    LZDP(size_t offset_bits = 0, size_t length_bits = 0)
        : offset_bits_(offset_bits),
          length_bits_(length_bits),
          use_flag_encoding_(false),
          match_engine_(0) {
        if (offset_bits_ == 0) offset_bits_ = 12;
        if (length_bits_ == 0) length_bits_ = 8;
        max_search_size_ = (size_t{1} << offset_bits_) - 1;
        max_look_size_ = (size_t{1} << length_bits_) - 1;
    }

    void autoBitWidth(size_t search_size, size_t lookahead_size) {
        offset_bits_ = calcBitWidth(search_size);
        length_bits_ = calcBitWidth(lookahead_size);
        max_search_size_ = (size_t{1} << offset_bits_) - 1;
        max_look_size_ = (size_t{1} << length_bits_) - 1;
    }

    size_t get_offset_bits() const { return offset_bits_; }
    size_t get_length_bits() const { return length_bits_; }

    void set_use_flag_encoding(bool use) { use_flag_encoding_ = use; }
    bool get_use_flag_encoding() const { return use_flag_encoding_; }

    void set_match_engine(int v) { match_engine_ = v; }
    int get_match_engine() const { return match_engine_; }

    void set_min_match(size_t v) { min_match_ = v; }
    size_t get_min_match_param() const { return min_match_; }

    std::vector<uint8_t> compress(const std::vector<uint8_t>& input,
                                   size_t search_size,
                                   size_t lookahead_size);

    std::vector<uint8_t> compress_dp(const std::vector<uint8_t>& input,
                                         size_t search_size,
                                         size_t lookahead_size,
                                         size_t range = 3);

    std::vector<Triple> compress_dp_triples(
        const std::vector<uint8_t>& input,
        size_t search_size,
        size_t lookahead_size,
        size_t range = 3);

    DPVisualization get_dp_visualization(
        const std::vector<uint8_t>& input,
        size_t search_size,
        size_t lookahead_size,
        size_t range = 3);

    std::vector<uint8_t> decompress(const std::vector<uint8_t>& input);
};

} // namespace algorithm
} // namespace compressor
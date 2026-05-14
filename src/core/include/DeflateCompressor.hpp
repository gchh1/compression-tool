#pragma once

#include <cstdint>
#include <vector>

#include "ICompressor.hpp"

namespace compressor {
namespace core {

class DeflateCompressor : public ICompressor {
public:
    DeflateCompressor()
        : slide_size_(4096),
          min_match_(3),
          max_chain_length_(256) {}

    auto compress(std::vector<uint8_t> original_data)
        -> CompressorResult override;

    auto decompress(std::vector<uint8_t> compressed_data)
        -> CompressorResult override;

    inline std::string get_algorithm_name(void) override {
        return "Deflate (LZ77 + Huffman)";
    }

    void set_search_size(size_t v) { slide_size_ = v; }
    size_t get_search_size() const { return slide_size_; }

    void set_slide_size(size_t v) { slide_size_ = v; }
    size_t get_slide_size() const { return slide_size_; }

    void set_min_match(size_t v) { min_match_ = v; }
    size_t get_min_match() const { return min_match_; }

    void set_max_chain_length(size_t v) { max_chain_length_ = v; }
    size_t get_max_chain_length() const { return max_chain_length_; }

    /** When true, compress/decompress use ``DPFlate`` with 3HfMTree (non-standard bitstream). */
    void set_use_3hfmtree(bool v) { use_3hfmtree_ = v; }
    bool get_use_3hfmtree() const { return use_3hfmtree_; }

    /** Unified Huffman slot width for 3HfM (sets both offset/length unless overridden). */
    void set_huffman_chunk_bits(size_t k) {
        huffman_offset_chunk_bits_ = k;
        huffman_length_chunk_bits_ = k;
    }
    size_t get_huffman_chunk_bits() const { return huffman_offset_chunk_bits_; }
    void set_huffman_offset_chunk_bits(size_t k) { huffman_offset_chunk_bits_ = k; }
    void set_huffman_length_chunk_bits(size_t k) { huffman_length_chunk_bits_ = k; }
    size_t get_huffman_offset_chunk_bits() const { return huffman_offset_chunk_bits_; }
    size_t get_huffman_length_chunk_bits() const { return huffman_length_chunk_bits_; }

    /** Used only when ``use_3hfmtree_`` (DPFlate kernel); classic Deflate ignores it. */
    void set_lookahead_size(size_t v) { lookahead_size_ = v; }
    size_t get_lookahead_size() const { return lookahead_size_; }

    void set_dp_sub_match_max(size_t v) { dp_sub_match_max_ = v; }
    size_t get_dp_sub_match_max() const { return dp_sub_match_max_; }

    void set_match_engine(int v) { match_engine_ = v; }
    int get_match_engine() const { return match_engine_; }

    void set_use_flag_encoding(bool v) { use_flag_encoding_ = v; }
    bool get_use_flag_encoding() const { return use_flag_encoding_; }

private:
    size_t slide_size_;
    size_t min_match_;
    size_t max_chain_length_;

    bool use_3hfmtree_{false};
    size_t lookahead_size_{256};
    size_t dp_sub_match_max_{6};
    int match_engine_{1};
    bool use_flag_encoding_{false};
    size_t huffman_offset_chunk_bits_{8};
    size_t huffman_length_chunk_bits_{8};
};

} // namespace core
} // namespace compressor

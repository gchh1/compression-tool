#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "ICompressor.hpp"
#include "Visualization.hpp"
#include "Deflate.hpp"
#include "Dpflate.hpp"
#include "LZSS.hpp"

namespace compressor::core {

class LZDPCompressor : public ICompressor {
public:
    LZDPCompressor();

    CompressorResult compress(std::vector<uint8_t> data) override;
    CompressorResult decompress(std::vector<uint8_t> data) override;
    std::string get_algorithm_name() override;

    algorithm::DPVisualization get_dp_visualization(const std::vector<uint8_t>& data,
                                                    size_t range = 0);

    void set_search_size(size_t v);
    size_t get_search_size() const;
    void set_lookahead_size(size_t v);
    size_t get_lookahead_size() const;
    void set_min_match(size_t v);
    size_t get_min_match() const;
    void set_dp_top(size_t v);
    size_t get_dp_top() const;
    void set_use_flag_encoding(bool v);
    bool get_use_flag_encoding() const;
    void set_match_engine(int v);
    int get_match_engine() const;

    algorithm::LZDPConfig& config() { return lzdp_; }
    const algorithm::LZDPConfig& getConfig() const { return lzdp_; }

private:
    algorithm::LZDPConfig lzdp_;
    size_t dp_range_{3};
};

class LZSSCompressor : public ICompressor {
public:
    LZSSCompressor();

    CompressorResult compress(std::vector<uint8_t> data) override;
    CompressorResult decompress(std::vector<uint8_t> data) override;
    std::string get_algorithm_name() override;

    void set_search_size(size_t v);
    size_t get_search_size() const;
    void set_lookahead_size(size_t v);
    size_t get_lookahead_size() const;
    void set_min_match(size_t v);
    size_t get_min_match() const;
    void set_use_flag_encoding(bool v);
    bool get_use_flag_encoding() const;

private:
    algorithm::LZSSConfig lzss_;
};

class DeflateCompressor : public ICompressor {
public:
    DeflateCompressor();

    CompressorResult compress(std::vector<uint8_t> data) override;
    CompressorResult decompress(std::vector<uint8_t> data) override;
    std::string get_algorithm_name() override;

    void set_search_size(size_t v);
    size_t get_search_size() const;
    void set_min_match(size_t v);
    size_t get_min_match() const;
    void set_max_chain_length(size_t v);
    size_t get_max_chain_length() const;
    void set_use_3hfmtree(bool v);
    bool get_use_3hfmtree() const;
    void set_huffman_chunk_bits(size_t k);
    size_t get_huffman_chunk_bits() const;
    void set_huffman_offset_chunk_bits(size_t k);
    void set_huffman_length_chunk_bits(size_t k);
    size_t get_huffman_offset_chunk_bits() const;
    size_t get_huffman_length_chunk_bits() const;
    void set_lookahead_size(size_t v);
    size_t get_lookahead_size() const;
    void set_dp_sub_match_max(size_t v);
    size_t get_dp_sub_match_max() const;
    void set_match_engine(int v);
    int get_match_engine() const;
    void set_use_flag_encoding(bool v);
    bool get_use_flag_encoding() const;

private:
    algorithm::DeflateConfig deflate_;
};

class DPFlateCompressor : public ICompressor {
public:
    DPFlateCompressor();

    CompressorResult compress(std::vector<uint8_t> data) override;
    CompressorResult decompress(std::vector<uint8_t> data) override;
    std::string get_algorithm_name() override;

    void set_search_size(size_t v);
    size_t get_search_size() const;
    void set_lookahead_size(size_t v);
    size_t get_lookahead_size() const;
    void set_min_match(size_t v);
    size_t get_min_match() const;
    void set_max_chain_length(size_t v);
    size_t get_max_chain_length() const;
    void set_dp_sub_match_max(size_t v);
    size_t get_dp_sub_match_max() const;
    void set_match_engine(int v);
    int get_match_engine() const;
    void set_use_flag_encoding(bool v);
    bool get_use_flag_encoding() const;
    void set_use_3hfmtree(bool v);
    bool get_use_3hfmtree() const;
    void set_huffman_chunk_bits(size_t k);
    size_t get_huffman_chunk_bits() const;
    void set_huffman_offset_chunk_bits(size_t k);
    void set_huffman_length_chunk_bits(size_t k);
    size_t get_huffman_offset_chunk_bits() const;
    size_t get_huffman_length_chunk_bits() const;

private:
    algorithm::DPflateConfig dpflate_;
};

}  // namespace compressor::core

#pragma once

#include <cstddef>

#include "ICompressor.hpp"

namespace compressor::core {

class GzipCompressor : public ICompressor {
public:
    GzipCompressor() = default;

    CompressorResult compress(std::vector<uint8_t> data) override;
    CompressorResult decompress(std::vector<uint8_t> data) override;
    std::string get_algorithm_name() override;

    void set_compression_level(size_t v) { compression_level_ = v; }
    size_t get_compression_level() const { return compression_level_; }

private:
    size_t compression_level_{6};
};

class BrotliCompressor : public ICompressor {
public:
    CompressorResult compress(std::vector<uint8_t> data) override;
    CompressorResult decompress(std::vector<uint8_t> data) override;
    std::string get_algorithm_name() override;

    void set_window_size(size_t v) { window_size_ = v; }
    size_t get_window_size() const { return window_size_; }
    void set_min_match(size_t v) { min_match_ = v; }
    size_t get_min_match() const { return min_match_; }
    void set_max_chain_length(size_t v) { max_chain_length_ = v; }
    size_t get_max_chain_length() const { return max_chain_length_; }

private:
    size_t window_size_{22};
    size_t min_match_{4};
    size_t max_chain_length_{256};
};

class ZstdCompressor : public ICompressor {
public:
    CompressorResult compress(std::vector<uint8_t> data) override;
    CompressorResult decompress(std::vector<uint8_t> data) override;
    std::string get_algorithm_name() override;

    void set_compression_level(size_t v) { compression_level_ = v; }
    size_t get_compression_level() const { return compression_level_; }

private:
    size_t compression_level_{3};
};

}  // namespace compressor::core

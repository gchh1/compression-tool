#pragma once

#include <cstdint>
#include <vector>

#include "ICompressor.hpp"

namespace compressor {
namespace core {
class LZSSCompressor : public ICompressor {
   public:
    LZSSCompressor()
        : dictionary_buffer_size_(4095),
          min_match_length_(3),
          max_match_length_(18) {}

    auto compress(std::vector<uint8_t> data) -> CompressorResult override;

    auto decompress(std::vector<uint8_t> data) -> CompressorResult override;

    inline auto get_algorithm_name(void) -> std::string override {
        return "LZSS";
    }

    void set_search_size(uint16_t v) { dictionary_buffer_size_ = v; }
    uint16_t get_search_size() const { return dictionary_buffer_size_; }

    void set_min_match(uint8_t v) { min_match_length_ = v; }
    uint8_t get_min_match() const { return min_match_length_; }

    void set_lookahead_size(uint8_t v) { max_match_length_ = v; }
    uint8_t get_lookahead_size() const { return max_match_length_; }

    void set_dictionary_buffer_size(uint16_t v) { dictionary_buffer_size_ = v; }
    uint16_t get_dictionary_buffer_size() const { return dictionary_buffer_size_; }

    void set_min_match_length(uint8_t v) { min_match_length_ = v; }
    uint8_t get_min_match_length() const { return min_match_length_; }

    void set_max_match_length(uint8_t v) { max_match_length_ = v; }
    uint8_t get_max_match_length() const { return max_match_length_; }

    void set_use_flag_encoding(bool v) { use_flag_encoding_ = v; }
    bool get_use_flag_encoding() const { return use_flag_encoding_; }

   private:
    uint16_t dictionary_buffer_size_;
    uint8_t min_match_length_;
    uint8_t max_match_length_;
    bool use_flag_encoding_{true};
};

}  // namespace core

}  // namespace compressor
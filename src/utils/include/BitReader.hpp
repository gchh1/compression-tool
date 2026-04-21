/**
 * @file BitReader.hpp
 * @author yhc
 * @brief Wrapper class for read bit
 * @version 0.1
 * @date 2026-04-21
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstdint>
#include <span>
#include <utility>
namespace compressor::utils {
class BitReader {
   public:
    // ===================================
    // Constructor and assignment
    // ===================================

    /* Delete default constructor and copy */
    BitReader() = delete;
    BitReader(const BitReader &) = delete;
    BitReader &operator=(const BitReader &) = delete;

    /** @brief Construct a BitReader by read */
    explicit BitReader(std::span<const uint8_t> read) : read_(read) {};

    /* Only allow move constructor */
    BitReader(BitReader &&that)
        : read_(std::exchange(that.read_, {})),
          byte_pos_(that.byte_pos_),
          bit_buffer_(that.bit_buffer_),
          bits_in_buffer_(that.bits_in_buffer_) {}

    /* Only allow move assignment */
    BitReader &operator=(BitReader &&that) {
        if (this != &that) {
            read_ = std::exchange(that.read_, {});
            byte_pos_ = that.byte_pos_;
            bit_buffer_ = that.bit_buffer_;
            bits_in_buffer_ = that.bits_in_buffer_;
        }
        return *this;
    }

    // ===================================
    // Method we need
    // ===================================

    /**
     * @brief Peek a bit from reader
     *
     * @return true
     * @return false
     */
    inline auto readBit() -> bool {
        if (bits_in_buffer_ == 0) {
            if (byte_pos_ >= read_.size()) {
                return false;
            }
            bit_buffer_ = read_[byte_pos_++];
            bits_in_buffer_ = 8;
        }
        int bit = (bit_buffer_ >> 7) & 1;
        bit_buffer_ <<= 1;
        bits_in_buffer_--;
        return true;
    }

    inline auto readBits(uint8_t count) -> bool {
        while (count--) {
        }
    }

   private:
    /** @brief Span resources that the class wrappered */
    std::span<const uint8_t> read_;

    size_t byte_pos_{0};
    uint8_t bit_buffer_{0};
    uint8_t bits_in_buffer_{0};
};

}  // namespace compressor::utils

/**
 * @file BitWritter.hpp
 * @author yhc
 * @brief Wrapper class for write bit
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
class BitWritter {
   public:
    // ===================================
    // Constructor and assignment
    // ===================================

    /* Delete default constructor and copy */
    BitWritter() = delete;
    BitWritter(const BitWritter &) = delete;
    BitWritter &operator=(const BitWritter &) = delete;

    /** @brief Construct a BitWritter by write */
    explicit BitWritter(std::span<uint8_t> write) : write_(write) {};

    /* Only allow move constructor */
    BitWritter(BitWritter &&that)
        : write_(std::exchange(that.write_, {})),
          byte_pos_(that.byte_pos_),
          bit_buffer_(that.bit_buffer_),
          bit_idx_(that.bit_idx_) {}

    /* Only allow move assignment */
    BitWritter &operator=(BitWritter &&that) {
        if (this != &that) {
            write_ = std::exchange(that.write_, {});
            byte_pos_ = that.byte_pos_;
            bit_buffer_ = that.bit_buffer_;
            bit_idx_ = that.bit_idx_;
        }
        return *this;
    }

    // ===================================
    // Method we need
    // ===================================

    /**
     * @brief Write a bit to the write_ in little-endian
     *
     * @return int
     */
    inline auto writeBit(uint8_t bit) -> bool {
        bit_buffer_ = (bit_buffer_ << 1) | (bit & 1);
        bit_idx_++;

        if (bit_idx_ == 8) {
            if (byte_pos_ >= write_.size()) {
                return false;
            }
            write_[byte_pos_++] = bit_buffer_;
            bit_buffer_ = 0;
            bit_idx_ = 0;
        }
        return true;
    }

    /**
     * @brief Write multiple bits, even a uint32 value
     *
     * @param value
     * @param count Number of bits we need to write
     * @return true
     * @return false
     */
    inline auto writeBits(uint32_t value, uint8_t count) -> bool {
        for (int i = count - 1; i >= 0; --i) {
            if (!writeBit((value >> i) & 1)) {
                return false;
            }
        }
        return true;
    }

    /**
     * @brief Called when the byte flow over
     *
     * @return size_t How many `bytes` we write into
     */
    auto flush() -> size_t {
        if (bit_idx_ > 0) {
            if (byte_pos_ < write_.size()) {
                write_[byte_pos_++] = bit_buffer_ << (8 - bit_idx_);
            }
            bit_idx_ = 0;
        }
        return byte_pos_;
    }

   private:
    /** @brief Span resources that the class wrappered */
    std::span<uint8_t> write_;

    size_t byte_pos_{0};
    uint8_t bit_buffer_{0};
    uint8_t bit_idx_{0};
};

}  // namespace compressor::utils

/**
 * @file BitWriter.hpp
 * @author yhc
 * @brief Wrapper class for write bit
 * @version 0.1
 * @date 2026-04-21
 *
 * @copyright Copyright (c) 2026
 *
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
namespace compressor::utils {
class BitWriter {
   public:
    // ===================================
    // Constructor and assignment
    // ===================================

    /* Delete default constructor and copy */
    BitWriter() = delete;
    BitWriter(const BitWriter &) = delete;
    BitWriter &operator=(const BitWriter &) = delete;

    /** @brief Construct a BitWriter by write */
    explicit BitWriter(std::span<uint8_t> write) : write_(write) {};

    BitWriter(std::span<uint8_t> write, uint64_t res_buffer, uint8_t res_idx)
        : write_(write), buffer_(res_buffer), buffer_idx_(res_idx) {}

    /* Only allow move constructor */
    BitWriter(BitWriter &&that)
        : write_(std::exchange(that.write_, {})),
          byte_pos_(that.byte_pos_),
          buffer_(that.buffer_),
          buffer_idx_(that.buffer_idx_),
          is_overflow_(that.is_overflow_) {}

    /* Only allow move assignment */
    BitWriter &operator=(BitWriter &&that) {
        if (this != &that) {
            write_ = std::exchange(that.write_, {});
            byte_pos_ = that.byte_pos_;
            buffer_ = that.buffer_;
            buffer_idx_ = that.buffer_idx_;
            is_overflow_ = that.is_overflow_;
        }
        return *this;
    }

    // ===================================
    // Method we need
    // ===================================

    auto isOverflow(void) const -> bool { return is_overflow_; }

    auto getBuffer(void) const -> uint64_t { return buffer_; }

    auto getBufferIdx(void) const -> uint8_t { return buffer_idx_; }

    auto getBytesWritten(void) const -> size_t { return byte_pos_; }

    /**
     * @brief Write a bit to the write_ in little-endian
     *
     * @return int
     */
    inline auto writeBit(uint8_t bit) -> void {
        buffer_ = (buffer_ << 1) | (bit & 1);
        buffer_idx_++;

        if (buffer_idx_ == 8) {
            buffer_idx_ = 0;
            if (byte_pos_ < write_.size()) {
                write_[byte_pos_++] = static_cast<uint8_t>(buffer_);
            } else {
                is_overflow_ = true;
            }
        }
    }

    /**
     * @brief Write multiple bits, even a uint64 value
     *
     * @param value
     * @param count Number of bits we need to write
     * @return true
     * @return false
     */
    inline auto writeBits(uint64_t value, uint8_t count) -> void {
        if (count == 0) {
            return;
        }

        // Mask the high bits
        value &= (1ULL << count) - 1;

        buffer_ = (buffer_ << count) | value;
        buffer_idx_ += count;

        while (buffer_idx_ >= 8) {
            buffer_idx_ -= 8;
            if (byte_pos_ < write_.size()) {
                write_[byte_pos_++] =
                    static_cast<uint8_t>(buffer_ >> buffer_idx_);
            } else {
                is_overflow_ = true;
            }
        }
    }

    /**
     * @brief Called when the byte flow over
     *
     * @return size_t How many `bytes` we write into
     */
    auto flush() -> size_t {
        if (buffer_idx_ > 0) {
            if (byte_pos_ < write_.size()) {
                write_[byte_pos_++] =
                    static_cast<uint8_t>(buffer_ << (8 - buffer_idx_));
            } else {
                is_overflow_ = true;
            }
            buffer_idx_ = 0;
        }
        return byte_pos_;
    }

   private:
    /** @brief Span resources that the class wrappered */
    std::span<uint8_t> write_;

    /** @brief Index for `write_` */
    size_t byte_pos_{0};

    /** @brief When the buffer_ is full, we write to `write_` at once */
    uint64_t buffer_{0};

    /** @brief  */
    uint8_t buffer_idx_{0};

    /** @brief true if reach the end of `write_` */
    bool is_overflow_{false};
};

}  // namespace compressor::utils

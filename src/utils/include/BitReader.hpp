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
    explicit BitReader(std::span<const uint8_t> read) : read_(read) {
        fillBuffer();
    }

    /** @brief Construct a BitReader to inherit the `buffer` */
    BitReader(std::span<const uint8_t> read, uint64_t res_buf, uint8_t res_idx)
        : read_(read), buffer_(res_buf), buffer_idx_(res_idx) {}

    /* Only allow move constructor */
    BitReader(BitReader &&that)
        : read_(std::exchange(that.read_, {})),
          byte_pos_(that.byte_pos_),
          buffer_(that.buffer_),
          buffer_idx_(that.buffer_idx_) {}

    /* Only allow move assignment */
    BitReader &operator=(BitReader &&that) {
        if (this != &that) {
            read_ = std::exchange(that.read_, {});
            byte_pos_ = that.byte_pos_;
            buffer_ = that.buffer_;
            buffer_idx_ = that.buffer_idx_;
        }
        return *this;
    }

    // ===================================
    // Method we need
    // ===================================

    auto isEOF(void) const -> bool { return is_eof_; }

    auto getBuffer(void) const -> uint64_t { return buffer_; }

    auto getBufferIdx(void) const -> uint8_t { return buffer_idx_; }

    /**
     * @brief Peek a bit by `MSB` first. Need to check `is_eof_` outside
     *
     * @return uint8_t
     */
    inline auto readBit() -> uint8_t {
        if (buffer_idx_ == 0) {
            fillBuffer();
            if (is_eof_) {
                return 0;
            }
        }
        return (buffer_ >> (--buffer_idx_)) & 1;
    }

    /**
     * @brief
     *
     * @param count maximum value `64`
     * @return uint64_t
     */
    inline auto readBits(uint8_t count) -> uint64_t {
        if (count == 0) {
            return 0;
        }

        if (buffer_idx_ < count) {
            fillBuffer();
            if (buffer_idx_ < count) {
                is_eof_ = true;
                return 0;
            }
        }

        buffer_idx_ -= count;
        return (buffer_ >> buffer_idx_) & ((1ULL << count) - 1);
    }

   private:
    /** @brief Span resources that the class wrappered */
    std::span<const uint8_t> read_;

    /** @brief Store the pos of read_ */
    size_t byte_pos_{0};

    /** @brief Data buffer that load in 64 bits CPU */
    uint64_t buffer_{0};

    /** @brief Point to the top of buffer */
    uint8_t buffer_idx_{0};

    /** @brief EOF flag*/
    bool is_eof_{false};

    /**
     * @brief Fill the buffer by byte
     *
     */
    inline auto fillBuffer(void) -> void {
        // Seize bytes to buffer if available
        while (byte_pos_ < read_.size() && buffer_idx_ <= 56) {
            buffer_ = (buffer_ << 8) | read_[byte_pos_++];
            buffer_idx_ += 8;
        }

        if (buffer_idx_ == 0) {
            is_eof_ = true;
        }
    }
};

}  // namespace compressor::utils

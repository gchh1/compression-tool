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

#include <atomic>
#include <cstdint>
#include <optional>
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

    /**
     * @brief Peek a bit by `MSB` first
     *
     * @return std::optional<uint8_t>
     */
    inline auto readBit() -> std::optional<uint8_t> {
        if (buffer_idx_ == 0) {
            fillBuffer();
            if (is_eof_) {
                return std::nullopt;
            }
        }

        return (buffer_ >> (buffer_idx_--)) & 1;
    }

    inline auto readBits(uint8_t count) -> std::optional<uint64_t> {
        if (buffer_idx_ < count - 1) {
            fillBuffer();
            if (is_eof_) {
                return std::nullopt;
            }
        }

        uint64_t bits = 0;
        while (count--) {
            auto temp = readBit();
            if (!temp) {
                return bits;
            }
            bits = (bits << 1) | temp;
        }
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

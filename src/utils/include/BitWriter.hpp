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
#include <cstring>
#include <span>
#include <utility>
namespace compressor::utils {
class BitWriter {
   public:
    // ===================================
    // Constructor and assignment
    // ===================================

    /* Delete default constructor and copy */
    BitWriter() = default;
    BitWriter(const BitWriter &) = delete;
    BitWriter &operator=(const BitWriter &) = delete;

    /** @brief Construct a BitWriter by write */
    explicit BitWriter(std::span<uint8_t> write) : write_(write) {};

    /** @brief Construct a BitWriter to inherit the `buffer` */
    BitWriter(std::span<uint8_t> write, uint64_t res_buffer, uint8_t res_idx)
        : write_(write), buffer_(res_buffer), buffer_idx_(res_idx) {}

    /* Only allow move constructor */
    BitWriter(BitWriter &&that)
        : write_(std::exchange(that.write_, {})),
          byte_pos_(that.byte_pos_),
          buffer_(that.buffer_),
          buffer_idx_(that.buffer_idx_) {}

    /* Only allow move assignment */
    BitWriter &operator=(BitWriter &&that) {
        if (this != &that) {
            write_ = std::exchange(that.write_, {});
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
     * @brief Return false if we don not have enough space to contain
     *
     * @param count
     * @return true
     * @return false
     */
    auto ensureSpace(size_t count) const -> bool {
        return byte_pos_ + (buffer_idx_ + count) / 8 <= write_.size();
    }

    /**
     * @brief Write a bit to the write_ in `little-endian`
     *
     * @return int
     */
    auto writeBit(uint8_t bit) -> void {
        buffer_ |= (static_cast<uint64_t>(bit & 1) << buffer_idx_);
        buffer_idx_++;

        if (buffer_idx_ == 8) {
            write_[byte_pos_++] =
                static_cast<uint8_t>(buffer_);  // 修复：LSB-first 直接强转
            buffer_ >>= 8;
            buffer_idx_ = 0;
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
    auto writeBits(uint64_t value, uint8_t count) -> void {
        if (count == 0) return;

        value &= (1ULL << count) - 1;
        buffer_ |= (value << buffer_idx_);
        buffer_idx_ += count;

        // 凑够 32 位直接一次性写入
        while (buffer_idx_ >= 32) {
            uint32_t out_word = static_cast<uint32_t>(buffer_);
            std::memcpy(write_.data() + byte_pos_, &out_word, 4);
            byte_pos_ += 4;

            buffer_ >>= 32;
            buffer_idx_ -= 32;
        }

        // 处理不够 32 位但够 8 位的情况
        while (buffer_idx_ >= 8) {
            write_[byte_pos_++] =
                static_cast<uint8_t>(buffer_);  // 修复：取底端字节
            buffer_ >>= 8;
            buffer_idx_ -= 8;
        }
    }

    /**
     * @brief
     *
     * @param src
     * @param count
     * @return size_t
     */
    auto writeBytes(const uint8_t *src, size_t count) -> size_t {
        while (buffer_idx_ >= 8) {
            write_[byte_pos_++] = static_cast<uint8_t>(buffer_);
            buffer_ >>= 8;
            buffer_idx_ -= 8;
        }

        size_t remain = write_.size() - byte_pos_;
        size_t to_copy = std::min(count, remain);

        // 修复：拷贝长度就是 to_copy，而不是 8 * count！
        std::memcpy(write_.data() + byte_pos_, src, to_copy);
        byte_pos_ += to_copy;

        return to_copy;
    }

    /**
     * @brief Called when the byte flow over
     *
     * @return size_t How many `bytes` we write into
     */
    auto flush() -> size_t {
        while (buffer_idx_ > 0) {
            write_[byte_pos_++] = static_cast<uint8_t>(buffer_);
            if (buffer_idx_ >= 8) {
                buffer_ >>= 8;
                buffer_idx_ -= 8;
            } else {
                buffer_ = 0;
                buffer_idx_ = 0;
            }
        }
        return byte_pos_;
    }

    /**
     * @brief Called when the source data change, but keep buffer
     *
     * @param write
     */
    auto changeSource(std::span<uint8_t> write) -> void {
        write_ = write;
        byte_pos_ = 0;
    }

    auto getBytesWritten(void) const -> size_t {
        return byte_pos_ + (buffer_idx_ / 8);
    }

    auto getSourceSize(void) const -> size_t { return write_.size(); }

    auto getRemainSize(void) const -> size_t {
        return write_.size() - byte_pos_ - buffer_idx_ / 8;
    }

    // ===================================
    // Private
    // ===================================
   private:
    /** @brief Span resources that the class wrappered */
    std::span<uint8_t> write_;

    /** @brief Index for `write_` */
    size_t byte_pos_{0};

    /** @brief When the buffer_ is full, we write to `write_` at once */
    uint64_t buffer_{0};

    /** @brief  */
    uint8_t buffer_idx_{0};
};

}  // namespace compressor::utils

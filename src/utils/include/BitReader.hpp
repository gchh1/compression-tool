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

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <utility>

namespace compressor::utils {

class BitReader {
   public:
    // ===================================
    // Constructor and assignment
    // ===================================

    /* Delete default constructor and copy */
    BitReader() = default;
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
          buffer_idx_(that.buffer_idx_),
          total_bits_consumed_(that.total_bits_consumed_) {}

    /* Only allow move assignment */
    BitReader &operator=(BitReader &&that) {
        if (this != &that) {
            read_ = std::exchange(that.read_, {});
            byte_pos_ = that.byte_pos_;
            buffer_ = that.buffer_;
            buffer_idx_ = that.buffer_idx_;
            total_bits_consumed_ = that.total_bits_consumed_;
        }
        return *this;
    }

    // ===================================
    // Method we need
    // ===================================
    /**
     * @brief
     *
     * @param count
     * @return true
     * @return false
     */
    auto ensureBits(uint8_t count) -> bool {
        if (buffer_idx_ < count) {
            fillBuffer();
        }
        return buffer_idx_ >= count;
    }

    /**
     * @brief Peek and consume a bit
     *
     * @return uint64_t
     */
    auto readBit(void) -> uint64_t { return readBits(1); }

    /**
     * @brief Peek and consume `count` bits
     *
     * @param count
     * @return uint64_t
     */
    auto readBits(uint8_t count) -> uint64_t {
        if (!ensureBits(count)) return 0;
        uint64_t res = peekBits(count);
        consumeBits(count);
        return res;
    }

    /**
     * @brief Peek and consume `count` bytes
     *
     * @param dst
     * @param count
     * @return size_t
     */
    auto readBytes(uint8_t *dst, size_t count) -> size_t {
        size_t copied = 0;

        // 1. 吐出已经被吸入 64-bit 寄存器的字节
        while (buffer_idx_ >= 8 && count > 0) {
            *dst++ = static_cast<uint8_t>(buffer_);
            buffer_ >>= 8;
            buffer_idx_ -= 8;
            total_bits_consumed_ += 8;
            count--;
            copied++;
        }

        // 2. 绕过寄存器，极速提取底层数据
        if (count > 0 && buffer_idx_ == 0) {
            size_t remain = read_.size() - byte_pos_;
            size_t to_copy = std::min(count, remain);

            if (to_copy > 0) {
                std::memcpy(dst, read_.data() + byte_pos_, to_copy);
                byte_pos_ += to_copy;
                total_bits_consumed_ += to_copy * 8;
                copied += to_copy;
                dst += to_copy;

                fillBuffer();  // 重新蓄水
            }
        }
        return copied;
    }

    /**
     * @brief Called when the source data change, but keep buffer
     *
     * @param read
     */
    auto changeSource(std::span<const uint8_t> read) -> void {
        uint8_t skip_bits = total_bits_consumed_ % 8;

        read_ = read;
        byte_pos_ = 0;
        buffer_ = 0;
        buffer_idx_ = 0;
        fillBuffer();

        // Skip bits already consumed from the first byte of the new span
        if (skip_bits > 0 && buffer_idx_ >= skip_bits) {
            buffer_ >>= skip_bits;
            buffer_idx_ -= skip_bits;
        }
    }

    /**
     * @brief Get the Byte Read object
     *
     * @return size_t
     */
    auto getByteRead(void) const -> size_t {
        return total_bits_consumed_ / 8;
    }

    /**
     * @brief Get the Source Size object
     *
     * @return size_t
     */
    auto getSourceSize(void) const -> size_t { return read_.size(); }

    auto getRemainSize(void) const -> size_t {
        return (read_.size() - byte_pos_) + (buffer_idx_ / 8);
    }

    auto getRemainingBits(void) const -> size_t {
        return (read_.size() - byte_pos_) * 8 + buffer_idx_;
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

    /** @brief Total bits consumed across all spans (for byte-alignment tracking) */
    size_t total_bits_consumed_{0};

    /**
     * @brief Greedily fill the buffer_
     *
     */
    auto fillBuffer() -> void {
        // If we are able to consume a word, do it!
        while (buffer_idx_ <= 32 && (read_.size() - byte_pos_) >= 4) {
            uint32_t next_word;
            std::memcpy(&next_word, read_.data() + byte_pos_, 4);

            buffer_ |= (static_cast<uint64_t>(next_word) << buffer_idx_);

            buffer_idx_ += 32;
            byte_pos_ += 4;
        }

        while (buffer_idx_ <= 56 && byte_pos_ < read_.size()) {
            uint64_t next_byte = read_[byte_pos_++];
            buffer_ |= (next_byte << buffer_idx_);
            buffer_idx_ += 8;
        }
    }

    /**
     * @brief
     *
     * @param count
     * @return uint64_t
     */
    auto peekBits(uint8_t count) const -> uint64_t {
        return buffer_ & ((1ULL << count) - 1);
    }

    /**
     * @brief
     *
     * @param count
     */
    auto consumeBits(uint8_t count) -> void {
        buffer_ >>= count;
        buffer_idx_ -= count;
        total_bits_consumed_ += count;
    }
};

}  // namespace compressor::utils

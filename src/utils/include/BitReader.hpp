#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace compressor::utils {

class BitReader {
public:
    BitReader() : read_(empty_span_), buffer_(0), buffer_idx_(0), byte_pos_(0) {}

    BitReader(std::span<const uint8_t> read, uint64_t buffer = 0, uint8_t buffer_idx = 0)
        : read_(read), buffer_(buffer), buffer_idx_(buffer_idx), byte_pos_(0) {}

    uint8_t readBit() {
        fill();
        if (buffer_idx_ == 0) return 0;
        buffer_idx_--;
        return static_cast<uint8_t>((buffer_ >> buffer_idx_) & 1);
    }

    uint64_t readBits(uint8_t nbits) {
        uint64_t value = 0;
        for (int i = 0; i < nbits; ++i) {
            value = (value << 1) | readBit();
        }
        return value;
    }

    uint64_t readBits(int nbits) {
        return readBits(static_cast<uint8_t>(nbits));
    }

    bool isEOF() const {
        return buffer_idx_ == 0 && byte_pos_ >= read_.size();
    }

    bool ensureBits(uint8_t needed) {
        fill();
        return buffer_idx_ >= needed;
    }

    size_t getRemainingBits() const {
        size_t total_bits = (read_.size() - byte_pos_) * 8 + buffer_idx_;
        return total_bits;
    }

    size_t getRemainSize() const {
        return read_.size() - byte_pos_;
    }

    size_t readBytes(uint8_t* dest, size_t count) {
        size_t copied = 0;
        while (copied < count && byte_pos_ < read_.size()) {
            dest[copied++] = read_[byte_pos_++];
        }
        return copied;
    }

    void alignToByte() {
        buffer_idx_ = 0;
        buffer_ = 0;
    }

    uint64_t getBuffer() const { return buffer_; }

    uint8_t getBufferIdx() const { return buffer_idx_; }

private:
    std::span<const uint8_t> empty_span_;
    std::span<const uint8_t> read_;
    uint64_t buffer_;
    uint8_t buffer_idx_;
    size_t byte_pos_;

    void fill() {
        while (buffer_idx_ <= 56 && byte_pos_ < read_.size()) {
            buffer_ = (buffer_ << 8) | read_[byte_pos_++];
            buffer_idx_ += 8;
        }
    }
};

}  // namespace compressor::utils
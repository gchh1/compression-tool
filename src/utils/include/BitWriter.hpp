#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace compressor::utils {

class BitWriter {
public:
    BitWriter() : write_(empty_span_), buffer_(0), buffer_idx_(0), bytes_written_(0) {}

    BitWriter(std::span<uint8_t> write, uint64_t buffer = 0, uint8_t buffer_idx = 0)
        : write_(write), buffer_(buffer), buffer_idx_(buffer_idx), bytes_written_(0) {}

    void writeBit(uint8_t bit) {
        buffer_ = (buffer_ << 1) | (bit & 1);
        buffer_idx_++;
        if (buffer_idx_ == 8) {
            flushByte();
        }
    }

    void writeBits(uint64_t value, uint8_t nbits) {
        for (int i = nbits - 1; i >= 0; --i) {
            writeBit(static_cast<uint8_t>((value >> i) & 1));
        }
    }

    void writeBits(uint64_t value, size_t nbits) {
        writeBits(value, static_cast<uint8_t>(nbits));
    }

    void writeBits(uint64_t value, int nbits) {
        writeBits(value, static_cast<uint8_t>(nbits));
    }

    bool ensureSpace(size_t bits) {
        size_t needed_bytes = (buffer_idx_ + bits + 7) / 8;
        return (bytes_written_ + needed_bytes) <= write_.size();
    }

    size_t flush() {
        size_t flushed = 0;
        while (buffer_idx_ > 0) {
            flushByte();
            flushed++;
        }
        return flushed;
    }

    size_t writeBytes(const uint8_t* data, size_t count) {
        for (size_t i = 0; i < count; ++i) {
            writeBits(data[i], 8);
        }
        return count;
    }

    void changeSource(std::span<uint8_t> src) {
        write_ = src;
        bytes_written_ = 0;
        buffer_ = 0;
        buffer_idx_ = 0;
    }

    size_t drainFullBytes() {
        size_t full_bytes = buffer_idx_ / 8;
        for (size_t i = 0; i < full_bytes; ++i) {
            flushByte();
        }
        return full_bytes;
    }

    void resetPendingBits() {
        buffer_ = 0;
        buffer_idx_ = 0;
    }

    size_t getBytesWritten() const { return bytes_written_; }

    uint64_t getBuffer() const { return buffer_; }

    uint8_t getBufferIdx() const { return buffer_idx_; }

private:
    std::span<uint8_t> write_;
    uint64_t buffer_;
    uint8_t buffer_idx_;
    size_t bytes_written_;
    std::span<uint8_t> empty_span_;

    void flushByte() {
        if (bytes_written_ < write_.size()) {
            uint8_t byte = 0;
            int bits_to_flush = (buffer_idx_ >= 8) ? 8 : static_cast<int>(buffer_idx_);
            if (bits_to_flush == 0) return;
            byte = static_cast<uint8_t>((buffer_ >> (buffer_idx_ - bits_to_flush)) & 0xFF);
            write_[bytes_written_] = byte;
            bytes_written_++;
            buffer_idx_ -= static_cast<uint8_t>(bits_to_flush);
            uint64_t mask = (buffer_idx_ > 0) ? ((1ULL << buffer_idx_) - 1) : 0;
            buffer_ &= mask;
        }
    }
};

}  // namespace compressor::utils
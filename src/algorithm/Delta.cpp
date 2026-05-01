#include "Delta.hpp"

#include <sys/types.h>

#include <cstddef>
#include <cstdint>

namespace compressor::algorithm {

// ===================================
// DeltaEncode
// ===================================

/**
 * @brief Construct a new Delta:: Delta object
 *
 * @param quality
 */
DeltaEncode::DeltaEncode(int quality) {
    shift_ = quality < 100 ? (100 - quality) / 14 : 0;
    buffer_.resize(BUFFER_SIZE_);
}

/**
 * @brief
 *
 * @return auto
 */
auto DeltaEncode::reset() -> void { prev_ = 0; }

/**
 * @brief
 *
 * @param read
 * @param write
 */
auto DeltaEncode::handle(AlgorithmStatus& status, bool is_last_chunk) -> void {
    while (true) {
        size_t read_remain = reader_.getRemainSize();
        size_t write_remain = writer_.getRemainSize();
        size_t process_bytes = std::min(BUFFER_SIZE_, std::min(read_remain, write_remain));

        if (process_bytes == 0) {
            if (read_remain == 0) {
                status.done = is_last_chunk;
                status.need_input = !is_last_chunk;
            }
            if (write_remain == 0 && read_remain > 0) {
                status.need_output = true;
            }
            return;
        }

        reader_.readBytes(buffer_.data(), process_bytes);

        for (size_t i = 0; i < process_bytes; ++i) {
            uint8_t pixel = (buffer_[i] >> shift_) << shift_;
            buffer_[i] = pixel - prev_;
            prev_ = pixel;
        }

        writer_.writeBytes(buffer_.data(), process_bytes);
    }
}

// ===================================
// Delta decode
// ===================================

DeltaDecode::DeltaDecode() { buffer_.resize(BUFFER_SIZE_); }

auto DeltaDecode::reset(void) -> void { prev_ = 0; }

/**
 * @brief
 *
 * @param read
 * @param write
 */
auto DeltaDecode::handle(AlgorithmStatus& status, bool is_last_chunk) -> void {
    while (true) {
        size_t read_remain = reader_.getRemainSize();
        size_t write_remain = writer_.getRemainSize();
        size_t process_bytes = std::min(BUFFER_SIZE_, std::min(read_remain, write_remain));

        if (process_bytes == 0) {
            if (read_remain == 0) {
                status.done = is_last_chunk;
                status.need_input = !is_last_chunk;
            }
            if (write_remain == 0 && read_remain > 0) {
                status.need_output = true;
            }
            return;
        }

        reader_.readBytes(buffer_.data(), process_bytes);

        for (size_t i = 0; i < process_bytes; ++i) {
            buffer_[i] += prev_;
            prev_ = buffer_[i];
        }

        writer_.writeBytes(buffer_.data(), process_bytes);
    }
}

std::vector<uint8_t> Delta::encode(std::vector<uint8_t> data, int quality) {
    DeltaEncode enc(quality);
    enc.reset();
    std::vector<uint8_t> out(data.size());
    AlgorithmStatus status = enc.process(data, out, true);
    out.resize(status.bytes_produced);
    return out;
}

std::vector<uint8_t> Delta::decode(std::vector<uint8_t> data) {
    DeltaDecode dec;
    dec.reset();
    std::vector<uint8_t> out(data.size() * 2);
    AlgorithmStatus status = dec.process(data, out, true);
    out.resize(status.bytes_produced);
    return out;
}

}  // namespace compressor::algorithm

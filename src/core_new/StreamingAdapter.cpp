#include "StreamingAdapter.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace compressor::core {

static void encodeU32BE(uint32_t val, std::vector<uint8_t>& out) {
    out.push_back(static_cast<uint8_t>((val >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((val >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((val >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(val & 0xFF));
}

static auto decodeU32BE(const uint8_t* p) -> uint32_t {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

auto StreamingCompressAdapter::process(std::span<const uint8_t> read,
                                       std::span<uint8_t> write,
                                       bool is_last_chunk)
    -> algorithm::AlgorithmStatus {

    if (!read.empty()) {
        input_buffer_.insert(input_buffer_.end(), read.begin(), read.end());
    }

    if (!finished_) {
        while (input_buffer_.size() >= chunk_size_) {
            std::vector<uint8_t> chunk_data(input_buffer_.begin(),
                                            input_buffer_.begin() + chunk_size_);
            input_buffer_.erase(input_buffer_.begin(),
                                input_buffer_.begin() + chunk_size_);

            auto compressed = compress_fn_(chunk_data);
            emitChunk(compressed);
        }

        if (is_last_chunk) {
            if (!input_buffer_.empty()) {
                auto compressed = compress_fn_(input_buffer_);
                input_buffer_.clear();
                emitChunk(compressed);
            }
            emitTerminator();
            finished_ = true;
        }
    }

    std::size_t written = copyToOutput(write);

    algorithm::AlgorithmStatus status{};
    status.bytes_consumed = read.size();
    status.bytes_produced = written;
    status.done = finished_ && output_pos_ >= output_buffer_.size();
    status.need_input = !finished_ && input_buffer_.size() < chunk_size_;
    status.need_output = output_pos_ < output_buffer_.size();
    return status;
}

auto StreamingCompressAdapter::reset() -> void {
    input_buffer_.clear();
    output_buffer_.clear();
    output_pos_ = 0;
    finished_ = false;
}

void StreamingCompressAdapter::emitChunk(const std::vector<uint8_t>& compressed) {
    encodeU32BE(static_cast<uint32_t>(compressed.size()), output_buffer_);
    output_buffer_.insert(output_buffer_.end(),
                          compressed.begin(), compressed.end());
}

void StreamingCompressAdapter::emitTerminator() {
    encodeU32BE(0, output_buffer_);
}

auto StreamingCompressAdapter::copyToOutput(std::span<uint8_t> write)
    -> std::size_t {
    std::size_t available = output_buffer_.size() - output_pos_;
    std::size_t to_write = std::min(available, write.size());
    if (to_write > 0) {
        std::memcpy(write.data(), output_buffer_.data() + output_pos_, to_write);
        output_pos_ += to_write;
        if (output_pos_ >= output_buffer_.size()) {
            output_buffer_.clear();
            output_pos_ = 0;
        }
    }
    return to_write;
}

auto StreamingDecompressAdapter::process(std::span<const uint8_t> read,
                                          std::span<uint8_t> write,
                                          bool is_last_chunk)
    -> algorithm::AlgorithmStatus {

    if (!read.empty()) {
        input_buffer_.insert(input_buffer_.end(), read.begin(), read.end());
    }

    if (!finished_) {
        while (tryDecodeNextChunk()) {}

        if (is_last_chunk && input_buffer_.empty()) {
            finished_ = true;
        }
    }

    std::size_t written = copyToOutput(write);

    algorithm::AlgorithmStatus status{};
    status.bytes_consumed = read.size();
    status.bytes_produced = written;
    status.done = finished_ && output_pos_ >= output_buffer_.size();
    status.need_input = !finished_ && input_buffer_.size() < 4;
    status.need_output = output_pos_ < output_buffer_.size();
    return status;
}

auto StreamingDecompressAdapter::reset() -> void {
    input_buffer_.clear();
    output_buffer_.clear();
    output_pos_ = 0;
    finished_ = false;
}

auto StreamingDecompressAdapter::tryDecodeNextChunk() -> bool {
    if (input_buffer_.size() < 4) return false;

    uint32_t chunk_sz = decodeU32BE(input_buffer_.data());

    if (chunk_sz == 0) {
        input_buffer_.erase(input_buffer_.begin(), input_buffer_.begin() + 4);
        finished_ = true;
        return false;
    }

    if (input_buffer_.size() < 4 + chunk_sz) return false;

    std::vector<uint8_t> chunk_data(input_buffer_.begin() + 4,
                                    input_buffer_.begin() + 4 + chunk_sz);
    input_buffer_.erase(input_buffer_.begin(),
                        input_buffer_.begin() + 4 + chunk_sz);

    auto decompressed = decompress_fn_(chunk_data);
    output_buffer_.insert(output_buffer_.end(),
                          decompressed.begin(), decompressed.end());
    return true;
}

auto StreamingDecompressAdapter::copyToOutput(std::span<uint8_t> write)
    -> std::size_t {
    std::size_t available = output_buffer_.size() - output_pos_;
    std::size_t to_write = std::min(available, write.size());
    if (to_write > 0) {
        std::memcpy(write.data(), output_buffer_.data() + output_pos_, to_write);
        output_pos_ += to_write;
        if (output_pos_ >= output_buffer_.size()) {
            output_buffer_.clear();
            output_pos_ = 0;
        }
    }
    return to_write;
}

}  // namespace compressor::core

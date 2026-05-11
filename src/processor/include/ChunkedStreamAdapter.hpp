#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "IAlgorithm.hpp"

namespace compressor::processor {

using CompressFn = std::function<std::vector<uint8_t>(const std::vector<uint8_t>&)>;
using DecompressFn = std::function<std::vector<uint8_t>(const std::vector<uint8_t>&)>;

constexpr size_t DEFAULT_STREAM_CHUNK_SIZE = 1 << 20;

class StreamingCompressAdapter : public algorithm::IAlgorithm {
public:
    StreamingCompressAdapter(CompressFn compress_fn,
                             size_t chunk_size = DEFAULT_STREAM_CHUNK_SIZE)
        : compress_fn_(std::move(compress_fn)),
          chunk_size_(chunk_size) {}

    auto process(std::span<const uint8_t> read, std::span<uint8_t> write,
                 bool is_last_chunk) -> algorithm::AlgorithmStatus override;

    auto reset() -> void override;

private:
    CompressFn compress_fn_;
    size_t chunk_size_;

    std::vector<uint8_t> input_buffer_;
    std::vector<uint8_t> output_buffer_;
    size_t output_pos_{0};
    bool finished_{false};

    auto flushOneChunk() -> bool;
    auto flushFinalChunk() -> void;
    auto emitChunk(const std::vector<uint8_t>& compressed) -> void;
    auto emitTerminator() -> void;
    auto copyToOutput(std::span<uint8_t> write) -> size_t;
};

class StreamingDecompressAdapter : public algorithm::IAlgorithm {
public:
    StreamingDecompressAdapter(DecompressFn decompress_fn)
        : decompress_fn_(std::move(decompress_fn)) {}

    auto process(std::span<const uint8_t> read, std::span<uint8_t> write,
                 bool is_last_chunk) -> algorithm::AlgorithmStatus override;

    auto reset() -> void override;

private:
    DecompressFn decompress_fn_;

    std::vector<uint8_t> input_buffer_;
    std::vector<uint8_t> output_buffer_;
    size_t output_pos_{0};
    bool finished_{false};

    auto tryDecodeNextChunk() -> bool;
    auto copyToOutput(std::span<uint8_t> write) -> size_t;
};

}  // namespace compressor::processor

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "IAlgorithm.hpp"

namespace compressor::core {

using CompressFn = std::function<std::vector<uint8_t>(const std::vector<uint8_t>&)>;
using DecompressFn = std::function<std::vector<uint8_t>(const std::vector<uint8_t>&)>;

inline constexpr std::size_t kStreamChunkDefaultBytes = 1 << 20;
inline constexpr std::size_t kStreamChunkMaxBytes = 128 * 1024 * 1024;

[[nodiscard]] inline std::size_t effective_stream_chunk_bytes(
    std::size_t requested) noexcept {
    if (requested == 0) {
        return kStreamChunkDefaultBytes;
    }
    return std::min(kStreamChunkMaxBytes, requested);
}

class StreamingCompressAdapter : public algorithm::IAlgorithm {
public:
    StreamingCompressAdapter(CompressFn compress_fn,
                             std::size_t chunk_size)
        : compress_fn_(std::move(compress_fn)),
          chunk_size_(chunk_size) {}

    auto process(std::span<const uint8_t> read, std::span<uint8_t> write,
                 bool is_last_chunk) -> algorithm::AlgorithmStatus override;

    auto reset() -> void override;

private:
    CompressFn compress_fn_;
    std::size_t chunk_size_;

    std::vector<uint8_t> input_buffer_;
    std::vector<uint8_t> output_buffer_;
    std::size_t output_pos_{0};
    bool finished_{false};

    void emitChunk(const std::vector<uint8_t>& compressed);
    void emitTerminator();
    auto copyToOutput(std::span<uint8_t> write) -> std::size_t;
};

class StreamingDecompressAdapter : public algorithm::IAlgorithm {
public:
    explicit StreamingDecompressAdapter(DecompressFn decompress_fn)
        : decompress_fn_(std::move(decompress_fn)) {}

    auto process(std::span<const uint8_t> read, std::span<uint8_t> write,
                 bool is_last_chunk) -> algorithm::AlgorithmStatus override;

    auto reset() -> void override;

private:
    DecompressFn decompress_fn_;

    std::vector<uint8_t> input_buffer_;
    std::vector<uint8_t> output_buffer_;
    std::size_t output_pos_{0};
    bool finished_{false};

    auto tryDecodeNextChunk() -> bool;
    auto copyToOutput(std::span<uint8_t> write) -> std::size_t;
};

}  // namespace compressor::core

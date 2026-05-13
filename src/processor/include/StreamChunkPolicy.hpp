#pragma once

#include <algorithm>
#include <cstddef>

namespace compressor::processor {

/**
 * @brief Single policy for file-streaming plaintext chunk size.
 *
 * Aligns ``docs/design/streaming-compression-design.md`` §2.3 ``chunk_size`` with
 * ``docs/design/lzdp-file-pipeline-design.md`` §1 (disk read / sliding window feed).
 *
 * Same value is used for: ``MemoryPool`` slot size and ``compressFile`` / directory
 * reads; ``StreamingCompressAdapter`` segment size (Deflate, LZSS, …); and the
 * natural ``process()`` read span for ``LZDP_OutOfCore`` / ``DPFlate`` COLLECT_INPUT
 * (one pipeline push per disk chunk — not independent per-algorithm sizes).
 */
inline constexpr std::size_t kStreamChunkMinBytes = 64 * 1024;
inline constexpr std::size_t kStreamChunkMaxBytes = 128 * 1024 * 1024;
inline constexpr std::size_t kStreamChunkDefaultBytes = 1 << 20;

[[nodiscard]] inline std::size_t effective_stream_chunk_bytes(
    std::size_t requested) noexcept {
    if (requested == 0) {
        return kStreamChunkDefaultBytes;
    }
    return std::min(kStreamChunkMaxBytes,
                    std::max(kStreamChunkMinBytes, requested));
}

/// Default ``StreamingCompressAdapter`` segment / legacy name (same as default clamp).
inline constexpr std::size_t DEFAULT_STREAM_CHUNK_SIZE = kStreamChunkDefaultBytes;

}  // namespace compressor::processor

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace compressor::viz {

class BackgroundWriter;

/// Collects per-chunk Shannon entropy during streaming compression and
/// writes a single-file .heat v2 file via BackgroundWriter.
///
/// .heat v2 format (mmap-friendly, all fixed-size):
///
///   Header (20 bytes):
///     magic:         u32 = 0x54414548 ("HEAT" LE)
///     version:       u32 = 2
///     total_chunks:  u32
///     chunk_bytes:   u32
///     avg_entropy:   f32
///
///   Chunk Data Array:
///     [total_chunks × f32 entropy]
///
/// Usage:
///   1. Construct with target .heat path.
///   2. Call onRawChunk() for each raw chunk read from disk, before
///      pushing it into the compression pipeline.
///   3. Call onCompressionFinish() after all chunks are processed.
///
/// Thread safety: onRawChunk / onCompressionFinish must be called from
/// the same thread (the file-reading / compression thread).
class EntropyCollector {
   public:
    /// .heat v2 magic: "HEAT" in little-endian.
    static constexpr uint32_t kMagic = 0x54414548;
    static constexpr uint32_t kVersion = 2;
    /// Default chunk size for entropy calculation (1 MiB).
    static constexpr size_t kDefaultChunkBytes = 1024;

    explicit EntropyCollector(const std::string& heat_path,
                              uint32_t chunk_bytes = kDefaultChunkBytes);
    ~EntropyCollector();

    EntropyCollector(const EntropyCollector&) = delete;
    EntropyCollector& operator=(const EntropyCollector&) = delete;

    /// Compute Shannon entropy of data[0..size) and buffer the result.
    /// @param data   Raw input bytes (not compressed).
    /// @param size   Number of valid bytes in data.
    void onRawChunk(const uint8_t* data, size_t size);

    /// Flush remaining buffers, write .heat v2 header + chunk data,
    /// finalize the file.
    void onCompressionFinish();

    /// Total chunks accumulated.
    uint32_t total_chunks() const { return total_chunks_; }
    /// Nominal chunk bytes used for entropy windows.
    uint32_t chunk_bytes() const { return chunk_bytes_; }
    /// Average entropy across all chunks (0.0 if no chunks).
    float avg_entropy() const;

    /// Compute Shannon entropy in a single pass over data[0..size).
    /// Uses a 256-bin histogram on the stack (zero heap allocation).
    static float computeEntropy(const uint8_t* data, size_t size);

   private:
    void ensureWriter();
    void flushEntropyBuffer();

    std::string heat_path_;
    std::string tmp_path_;
    uint32_t chunk_bytes_;
    uint32_t total_chunks_ = 0;
    double entropy_sum_ = 0.0;  // running sum for avg_entropy

    // BackgroundWriter for async chunk entropy writes
    std::unique_ptr<BackgroundWriter> writer_;

    // Buffered entropy values (float32 per chunk) — flushed to writer
    // when the buffer reaches kBufferFlushCount entries.
    static constexpr size_t kBufferFlushCount = 256;
    std::vector<float> entropy_buf_;
};

}  // namespace compressor::viz

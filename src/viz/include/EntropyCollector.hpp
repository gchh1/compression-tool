#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace compressor::viz {

/// Collects per-chunk compression statistics during streaming compression and
/// writes a single-file .heat file.
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
/// .heat v3 format (per-chunk compression ratio):
///
///   Header (28 bytes):
///     magic:         u32 = 0x54414548 ("HEAT" LE)
///     version:       u32 = 3
///     total_chunks:  u32
///     chunk_bytes:   u32
///     total_input:   u64
///     total_output:  u64
///
///   Chunk Data Array:
///     [total_chunks × f32 compression_ratio]
///
/// Usage:
///   1. Construct with target .heat path.
///   2. Call onRawChunk() for each raw chunk read from disk, before
///      pushing it into the compression pipeline.
///   3. Call onChunkOutput() after draining compressed output for the chunk.
///   4. Call onCompressionFinish() after all chunks are processed.
///
/// Thread safety: onRawChunk / onCompressionFinish must be called from
/// the same thread (the file-reading / compression thread).
class EntropyCollector {
   public:
    /// .heat magic: "HEAT" in little-endian.
    static constexpr uint32_t kMagic = 0x54414548;
    static constexpr uint32_t kVersion2 = 2;
    static constexpr uint32_t kVersion3 = 3;
    /// Default chunk size (1 MiB).
    static constexpr size_t kDefaultChunkBytes = 1024;

    explicit EntropyCollector(const std::string& heat_path,
                              uint32_t chunk_bytes = kDefaultChunkBytes);
    ~EntropyCollector();

    EntropyCollector(const EntropyCollector&) = delete;
    EntropyCollector& operator=(const EntropyCollector&) = delete;

    /// Compute Shannon entropy of data[0..size) and buffer the result.
    void onRawChunk(const uint8_t* data, size_t size);

    /// Record the compressed output size for the most recent chunk.
    /// Must be called after onRawChunk() and the corresponding
    /// pipeline drain — once per chunk.
    /// When at least one chunk has output recorded, onCompressionFinish()
    /// writes .heat v3 (compression ratios); otherwise v2 (entropy only).
    void onChunkOutput(size_t output_bytes);

    /// Flush remaining buffers, write .heat header + chunk data,
    /// finalize the file.
    void onCompressionFinish();

    /// Total chunks accumulated.
    uint32_t total_chunks() const { return total_chunks_; }
    /// Nominal chunk bytes used.
    uint32_t chunk_bytes() const { return chunk_bytes_; }
    /// Average entropy across all chunks (0.0 if no chunks).
    float avg_entropy() const;
    /// Total raw input bytes across all chunks.
    uint64_t total_input() const { return total_input_; }
    /// Total compressed output bytes across all chunks.
    uint64_t total_output() const { return total_output_; }

    /// Compute Shannon entropy in a single pass over data[0..size).
    /// Uses a 256-bin histogram on the stack (zero heap allocation).
    static float computeEntropy(const uint8_t* data, size_t size);

   private:
    void flushEntropyBuffer();

    std::string heat_path_;
    std::string tmp_path_;
    std::ofstream tmp_file_;
    uint32_t chunk_bytes_;
    uint32_t total_chunks_ = 0;
    double entropy_sum_ = 0.0;
    uint64_t total_input_ = 0;
    uint64_t total_output_ = 0;

    // Per-chunk output sizes for v3 (paired with entropy entries by index).
    std::vector<uint32_t> chunk_output_sizes_;

    // Buffered entropy values (float32 per chunk) — flushed to temp file
    // when the buffer reaches kBufferFlushCount entries.
    static constexpr size_t kBufferFlushCount = 256;
    std::vector<float> entropy_buf_;
};

}  // namespace compressor::viz

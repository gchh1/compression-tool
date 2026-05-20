#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "AlgorithmFactory.hpp"

namespace compressor::api {

using AlgorithmID = core::AlgorithmID;

struct CompressResult {
    std::vector<uint8_t> data;
    size_t original_size{0};
    size_t compressed_size{0};
    double compression_ratio{0.0};
    double time_ms{0.0};
    bool success{false};
    std::string error_message;
};

struct WCXUnpackResult {
    bool success{false};
    uint8_t algo_code{0};
    size_t original_size{0};
    size_t compressed_size{0};
    bool is_folder{false};
    std::string original_filename;
    std::vector<uint8_t> payload;
    std::string error_message;
};

/// Compress a single buffer with the given algorithm chain.
/// Pipeline knobs match ``compressFile`` when pointers are non-null (otherwise struct defaults).
/// @param streaming_compress_chunk_bytes  Passed to ``createAlgorithm`` / ``MemoryPool``; ``0`` clamps via
///                                        ``processor::effective_stream_chunk_bytes``.
auto compress(const std::vector<uint8_t>& data,
              std::span<const AlgorithmID> chain,
              const core::LzdpWholeFileParams* lzdp_whole_file = nullptr,
              const core::DpflatePipelineParams* dpflate_pipeline = nullptr,
              const core::DeflatePipelineParams* deflate_pipeline = nullptr,
              std::size_t streaming_compress_chunk_bytes = 0) -> CompressResult;

/// Decompress a single buffer with the given algorithm chain (same optional pipeline pointers /
/// chunk size as ``compress`` for symmetric ``MemoryPool`` sizing; ignored by ``Inflate`` / adapters
/// that do not read these fields).
///
/// **GUI default “strategy 1”**: the caller already holds the full compressed payload in ``data``;
/// this function still builds a ``processor::Pipeline`` with ``memory::MemoryPool`` (``MemoryPool.hpp``)
/// so ``StreamProcessor`` can reuse **fixed-size output chunks** while draining decoded bytes into
/// ``result.data`` — the pool does **not** replace storing the full input/output as contiguous vectors.
auto decompress(const std::vector<uint8_t>& data,
                  std::span<const AlgorithmID> chain,
                  const core::LzdpWholeFileParams* lzdp_whole_file = nullptr,
                  const core::DpflatePipelineParams* dpflate_pipeline = nullptr,
                  const core::DeflatePipelineParams* deflate_pipeline = nullptr,
                  std::size_t streaming_compress_chunk_bytes = 0) -> CompressResult;

auto pack_wcx(const std::vector<uint8_t>& compressed_data,
              AlgorithmID algorithm,
              size_t original_size,
              const std::string& original_filename = "",
              bool is_folder = false) -> std::vector<uint8_t>;

auto unpack_wcx(const std::vector<uint8_t>& data) -> WCXUnpackResult;

#ifndef __EMSCRIPTEN__

/// Cooperative cancel for ``compressFile`` (GUI worker sets true while C++ is in the read loop).
void set_streaming_compress_cancel_requested(bool requested);

/// Streaming single-file compression: read input in chunks, write to disk.
/// Writes to `output_path + ".part"` then renames to `output_path` on success.
/// @param stream_chunk_bytes  Read buffer / pool chunk size; `0` means default (1 MiB).
///                            Clamped to 64 KiB .. 64 MiB to match GUI limits.
auto compressFile(const std::string& input_path,
                  const std::string& output_path,
                  std::span<const AlgorithmID> chain,
                  size_t stream_chunk_bytes = 0,
                  uint32_t file_compress_opts = core::kFileCompressOptsNone,
                  const core::LzdpWholeFileParams* lzdp_whole_file = nullptr,
                  const core::DpflatePipelineParams* dpflate_pipeline = nullptr,
                  const core::DeflatePipelineParams* deflate_pipeline = nullptr)
    -> CompressResult;

/// Like compressFile, but also writes visualization events to `viz_path`.
/// The compress algorithm must inherit from AlgorithmBase (e.g. DPFlate);
/// algorithms wrapped in StreamingCompressAdapter (Deflate, Brotli) are
/// not yet supported for visualization.
auto compressFileWithViz(const std::string& input_path,
                         const std::string& output_path,
                         const std::string& viz_path,
                         std::span<const AlgorithmID> chain,
                         size_t stream_chunk_bytes = 0) -> CompressResult;

/// WCX single-file decompression to disk (see ``docs/design/streaming-workspace-spec.md`` / GUI engine).
///
/// - **Strategy 1** (typical Python default): read whole WCX (or whole payload) into memory,
///   decompress to a full plaintext buffer, then write the file in one go after decode completes.
///   ``MemoryPool`` is used **inside** ``decompressFile``'s ``Pipeline`` for intermediate chunks only.
/// - **Strategy 2** (concept / pipeline): plaintext produced in streaming ``pull`` chunks and
///   written incrementally (not "split output file in half"; that is unrelated to LZDP Phase 2
///   **two-block** temp-A/temp-B rolling I/O in ``streaming-compression-design.md`` §3.2).
/// - **Strategy 3** (this API when used from GUI with native flag): read WCX payload in bounded
///   chunks from disk, push through ``Pipeline``, drain pulls to the output file.
auto decompressFile(const std::string& input_path,
                    const std::string& output_path,
                    std::span<const AlgorithmID> chain,
                    size_t stream_chunk_bytes = 0) -> CompressResult;

#endif  // __EMSCRIPTEN__

}  // namespace compressor::api

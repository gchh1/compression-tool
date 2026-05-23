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

/// In-memory compression. Algorithm parameters are read from the global
/// ``CompressionConfig`` (loaded once at startup). Pass ``overrides_json``
/// for per-call ADE delta.
auto compress(const std::vector<uint8_t>& data,
              std::span<const AlgorithmID> chain,
              std::size_t streaming_compress_chunk_bytes = 0,
              const std::string& overrides_json = "") -> CompressResult;

/// In-memory decompression.
auto decompress(const std::vector<uint8_t>& data,
                std::span<const AlgorithmID> chain,
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

/// Unified streaming file compression.
///
/// Writes WCX to ``output_path``. When ``viz_path`` is non-empty, visualization
/// events are written in the same pass. When ``heat_path`` is non-empty, per-chunk
/// Shannon entropy is written. All three outputs are produced in one streaming pass.
///
/// Algorithm parameters are read from the global ``CompressionConfig``.
/// Pass ``overrides_json`` (a JSON object with per-key values) for ADE per-file delta.
auto compressFile(const std::string& input_path,
                  const std::string& output_path,
                  std::span<const AlgorithmID> chain,
                  size_t stream_chunk_bytes = 0,
                  const std::string& viz_path = "",
                  const std::string& heat_path = "",
                  const std::string& overrides_json = "")
    -> CompressResult;

/// WCX single-file decompression to disk.
auto decompressFile(const std::string& input_path,
                    const std::string& output_path,
                    std::span<const AlgorithmID> chain,
                    size_t stream_chunk_bytes = 0) -> CompressResult;

#endif  // __EMSCRIPTEN__

}  // namespace compressor::api

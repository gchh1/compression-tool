#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "AlgorithmFactory.hpp"
#include "BlockProfile.hpp"

namespace compressor::api {

using AlgorithmID = core::AlgorithmID;
using BlockInfo = algorithm::BlockInfo;
using BlockProfile = algorithm::BlockProfile;

struct CompressResult {
    std::vector<uint8_t> data;
    size_t original_size{0};
    size_t compressed_size{0};
    double compression_ratio{0.0};
    double time_ms{0.0};
    bool success{false};
    std::string error_message;
    std::optional<BlockProfile> block_profile;
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
auto compress(const std::vector<uint8_t>& data,
              std::span<const AlgorithmID> chain) -> CompressResult;

/// Decompress a single buffer with the given algorithm chain.
auto decompress(const std::vector<uint8_t>& data,
                std::span<const AlgorithmID> chain) -> CompressResult;

auto pack_wcx(const std::vector<uint8_t>& compressed_data,
              AlgorithmID algorithm,
              size_t original_size,
              const std::string& original_filename = "",
              bool is_folder = false) -> std::vector<uint8_t>;

auto unpack_wcx(const std::vector<uint8_t>& data) -> WCXUnpackResult;

#ifndef __EMSCRIPTEN__

/// Streaming single-file compression: read input in chunks, write to disk.
/// Writes to `output_path + ".part"` then renames to `output_path` on success.
/// @param stream_chunk_bytes  Read buffer / pool chunk size; `0` means default (1 MiB).
///                            Clamped to 64 KiB .. 64 MiB to match GUI limits.
auto compressFile(const std::string& input_path,
                  const std::string& output_path,
                  std::span<const AlgorithmID> chain,
                  size_t stream_chunk_bytes = 0) -> CompressResult;

/// Like compressFile, but also writes visualization events to `viz_path`.
/// The compress algorithm must inherit from AlgorithmBase (e.g. DPFlate);
/// algorithms wrapped in StreamingCompressAdapter (Deflate, Brotli) are
/// not yet supported for visualization.
/// On success, result.block_profile carries the viz data reference.
auto compressFileWithViz(const std::string& input_path,
                         const std::string& output_path,
                         const std::string& viz_path,
                         std::span<const AlgorithmID> chain,
                         size_t stream_chunk_bytes = 0) -> CompressResult;

/// Streaming single-file decompression: read archive in chunks, write to disk.
/// Writes to `output_path + ".part"` then renames to `output_path` on success.
auto decompressFile(const std::string& input_path,
                    const std::string& output_path,
                    std::span<const AlgorithmID> chain,
                    size_t stream_chunk_bytes = 0) -> CompressResult;

#endif  // __EMSCRIPTEN__

}  // namespace compressor::api

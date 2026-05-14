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

struct WebFile {
    std::string name;
    std::vector<uint8_t> content;
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
auto decompress(const std::vector<uint8_t>& data,
                  std::span<const AlgorithmID> chain,
                  const core::LzdpWholeFileParams* lzdp_whole_file = nullptr,
                  const core::DpflatePipelineParams* dpflate_pipeline = nullptr,
                  const core::DeflatePipelineParams* deflate_pipeline = nullptr,
                  std::size_t streaming_compress_chunk_bytes = 0) -> CompressResult;

/// Pack multiple files into a compressed archive using an algorithm chain.
auto packAndCompress(const std::vector<WebFile>& files,
                     std::span<const AlgorithmID> chain)
    -> std::vector<uint8_t>;

/// Unpack a compressed archive back into individual files.
auto decompressAndUnpack(const std::vector<uint8_t>& data)
    -> std::vector<WebFile>;

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

/// Streaming single-file decompression: read archive in chunks, write to disk.
/// Writes to `output_path + ".part"` then renames to `output_path` on success.
auto decompressFile(const std::string& input_path,
                    const std::string& output_path,
                    std::span<const AlgorithmID> chain,
                    size_t stream_chunk_bytes = 0) -> CompressResult;

/// Recursively pack and compress a directory into an archive file.
/// Writes to `output_path + ".part"` then renames to `output_path` on success.
auto compressDirectory(const std::string& dir_path,
                       const std::string& output_path,
                       std::span<const AlgorithmID> chain,
                       size_t stream_chunk_bytes = 0,
                       uint32_t file_compress_opts = core::kFileCompressOptsNone,
                       const core::LzdpWholeFileParams* lzdp_whole_file = nullptr,
                       const core::DpflatePipelineParams* dpflate_pipeline = nullptr,
                       const core::DeflatePipelineParams* deflate_pipeline = nullptr)
    -> CompressResult;

/// Unpack a compressed archive to disk, preserving directory structure.
auto decompressAndUnpackToDisk(const std::string& input_path,
                                const std::string& output_dir)
    -> CompressResult;

#endif  // __EMSCRIPTEN__

}  // namespace compressor::api

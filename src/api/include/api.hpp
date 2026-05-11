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
auto compress(const std::vector<uint8_t>& data,
              std::span<const AlgorithmID> chain) -> CompressResult;

/// Decompress a single buffer with the given algorithm chain.
auto decompress(const std::vector<uint8_t>& data,
                std::span<const AlgorithmID> chain) -> CompressResult;

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

/// Streaming single-file compression: read input in chunks, write to disk.
/// @param stream_chunk_bytes  Read buffer / pool chunk size; `0` means default (1 MiB).
///                            Clamped to 64 KiB .. 64 MiB to match GUI limits.
auto compressFile(const std::string& input_path,
                  const std::string& output_path,
                  std::span<const AlgorithmID> chain,
                  size_t stream_chunk_bytes = 0) -> CompressResult;

/// Streaming single-file decompression: read archive in chunks, write to disk.
auto decompressFile(const std::string& input_path,
                    const std::string& output_path,
                    std::span<const AlgorithmID> chain,
                    size_t stream_chunk_bytes = 0) -> CompressResult;

/// Recursively pack and compress a directory into an archive file.
auto compressDirectory(const std::string& dir_path,
                       const std::string& output_path,
                       std::span<const AlgorithmID> chain,
                       size_t stream_chunk_bytes = 0) -> CompressResult;

/// Unpack a compressed archive to disk, preserving directory structure.
auto decompressAndUnpackToDisk(const std::string& input_path,
                                const std::string& output_dir)
    -> CompressResult;

#endif  // __EMSCRIPTEN__

}  // namespace compressor::api

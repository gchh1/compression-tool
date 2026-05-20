#pragma once

#include <cstddef>
#include <cstdint>
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
    size_t bytes_processed{0};
    bool cancelled{false};
};

struct WCXUnpackResult {
    bool success{false};
    uint8_t algo_code{0};
    size_t original_size{0};
    size_t compressed_size{0};
    bool is_folder{false};
    bool web_dict_preprocess{false};
    std::string original_filename;
    std::vector<uint8_t> payload;
    std::string error_message;
};

auto compress(const std::vector<uint8_t>& data,
              std::span<const AlgorithmID> chain,
              const core::LzdpWholeFileParams* lzdp_whole_file = nullptr,
              const core::DpflatePipelineParams* dpflate_pipeline = nullptr,
              const core::DeflatePipelineParams* deflate_pipeline = nullptr,
              const core::LzssPipelineParams* lzss_pipeline = nullptr,
              std::size_t streaming_compress_chunk_bytes = 0) -> CompressResult;

auto decompress(const std::vector<uint8_t>& data,
                std::span<const AlgorithmID> chain,
                const core::LzdpWholeFileParams* lzdp_whole_file = nullptr,
                const core::DpflatePipelineParams* dpflate_pipeline = nullptr,
                const core::DeflatePipelineParams* deflate_pipeline = nullptr,
                const core::LzssPipelineParams* lzss_pipeline = nullptr,
                std::size_t streaming_compress_chunk_bytes = 0) -> CompressResult;

auto pack_wcx(const std::vector<uint8_t>& compressed_data,
              AlgorithmID algorithm,
              size_t original_size,
              const std::string& original_filename = "",
              bool is_folder = false,
              bool web_dict_preprocess = false) -> std::vector<uint8_t>;

auto unpack_wcx(const std::vector<uint8_t>& data) -> WCXUnpackResult;

#ifndef __EMSCRIPTEN__
void set_streaming_compress_cancel_requested(bool requested);

auto compressFile(const std::string& input_path,
                  const std::string& output_path,
                  std::span<const AlgorithmID> chain,
                  size_t stream_chunk_bytes = 0,
                  uint32_t file_compress_opts = core::kFileCompressOptsNone,
                  const core::LzdpWholeFileParams* lzdp_whole_file = nullptr,
                  const core::DpflatePipelineParams* dpflate_pipeline = nullptr,
                  const core::DeflatePipelineParams* deflate_pipeline = nullptr,
                  const core::LzssPipelineParams* lzss_pipeline = nullptr)
    -> CompressResult;

auto decompressFile(const std::string& input_path,
                    const std::string& output_path,
                    std::span<const AlgorithmID> chain,
                    size_t stream_chunk_bytes = 0,
                    const core::LzssPipelineParams* lzss_pipeline = nullptr,
                    const core::DpflatePipelineParams* dpflate_pipeline = nullptr,
                    const core::DeflatePipelineParams* deflate_pipeline = nullptr)
    -> CompressResult;

/// Directory WCX (not yet on algorithm_new streaming); returns ``success=false``.
auto compressDirectory(const std::string& dir_path,
                       const std::string& output_path,
                       std::span<const AlgorithmID> chain,
                       size_t stream_chunk_bytes = 0,
                       uint32_t file_compress_opts = core::kFileCompressOptsNone,
                       const core::LzdpWholeFileParams* lzdp_whole_file = nullptr,
                       const core::DpflatePipelineParams* dpflate_pipeline = nullptr,
                       const core::DeflatePipelineParams* deflate_pipeline = nullptr,
                       const core::LzssPipelineParams* lzss_pipeline = nullptr)
    -> CompressResult;
#endif

}  // namespace compressor::api

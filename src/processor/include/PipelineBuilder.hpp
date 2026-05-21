#pragma once

#include <memory>
#include <vector>

#include "AlgorithmFactory.hpp"
#include "MemoryPool.hpp"
#include "Pipeline.hpp"

namespace compressor::processor {

auto buildCompressionPipeline(
    const std::vector<core::AlgorithmID>& chain,
    std::shared_ptr<memory::MemoryPool> pool = nullptr,
    uint32_t file_compress_opts = core::kFileCompressOptsNone,
    const core::LzdpWholeFileParams* lzdp_whole_file = nullptr,
    std::size_t streaming_compress_chunk_bytes = 0,
    const core::DpflatePipelineParams* dpflate_pipeline = nullptr,
    const core::DeflatePipelineParams* deflate_pipeline = nullptr,
    const core::ImageCompressParams* image_compress = nullptr)
    -> std::unique_ptr<Pipeline>;

auto buildDecompressionPipeline(
    const std::vector<core::AlgorithmID>& compression_chain,
    std::shared_ptr<memory::MemoryPool> pool = nullptr)
    -> std::unique_ptr<Pipeline>;

}  // namespace compressor::processor

#pragma once

#include <memory>
#include <vector>

#include "AlgorithmFactory.hpp"
#include "MemoryPool.hpp"
#include "Pipeline.hpp"

namespace compressor::processor {

auto buildCompressionPipeline(
    const std::vector<core::AlgorithmID>& chain,
    std::shared_ptr<memory::MemoryPool> pool = nullptr)
    -> std::unique_ptr<Pipeline>;

auto buildDecompressionPipeline(
    const std::vector<core::AlgorithmID>& compression_chain,
    std::shared_ptr<memory::MemoryPool> pool = nullptr)
    -> std::unique_ptr<Pipeline>;

}  // namespace compressor::processor

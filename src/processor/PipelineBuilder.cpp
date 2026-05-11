#include "PipelineBuilder.hpp"

namespace compressor::processor {

auto buildCompressionPipeline(
    const std::vector<core::AlgorithmID>& chain,
    std::shared_ptr<memory::MemoryPool> pool)
    -> std::unique_ptr<Pipeline> {
    std::vector<std::unique_ptr<algorithm::IAlgorithm>> algos;
    for (auto id : chain) {
        if (auto a = core::createAlgorithm(id)) {
            algos.push_back(std::move(a));
        }
    }
    return std::make_unique<Pipeline>(std::move(algos), std::move(pool));
}

auto buildDecompressionPipeline(
    const std::vector<core::AlgorithmID>& compression_chain,
    std::shared_ptr<memory::MemoryPool> pool)
    -> std::unique_ptr<Pipeline> {
    std::vector<std::unique_ptr<algorithm::IAlgorithm>> algos;
    for (auto it = compression_chain.rbegin(); it != compression_chain.rend(); ++it) {
        if (auto a = core::createAlgorithm(core::getDecompressorID(*it))) {
            algos.push_back(std::move(a));
        }
    }
    return std::make_unique<Pipeline>(std::move(algos), std::move(pool));
}

}  // namespace compressor::processor

#include "PipelineBuilder.hpp"

namespace compressor::processor {

auto buildCompressionPipeline(
    const std::vector<core::AlgorithmID>& chain,
    std::shared_ptr<memory::MemoryPool> pool,
    uint32_t file_compress_opts,
    const core::LzdpWholeFileParams* lzdp_whole_file,
    std::size_t streaming_compress_chunk_bytes,
    const core::DpflatePipelineParams* dpflate_pipeline,
    const core::DeflatePipelineParams* deflate_pipeline)
    -> std::unique_ptr<Pipeline> {
    std::vector<std::unique_ptr<algorithm::IAlgorithm>> algos;
    for (auto id : chain) {
        const core::LzdpWholeFileParams* lz =
            (id == core::AlgorithmID::LZDP) ? lzdp_whole_file : nullptr;
        const core::DpflatePipelineParams* df =
            (id == core::AlgorithmID::DPFlate) ? dpflate_pipeline : nullptr;
        const core::DeflatePipelineParams* dfl =
            (id == core::AlgorithmID::Deflate) ? deflate_pipeline : nullptr;
        if (auto a = core::createAlgorithm(id, file_compress_opts, lz,
                                           streaming_compress_chunk_bytes, df,
                                           dfl)) {
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
        if (auto a = core::createAlgorithm(core::getDecompressorID(*it),
                                           core::kFileCompressOptsNone, nullptr, 0,
                                           nullptr, nullptr)) {
            algos.push_back(std::move(a));
        }
    }
    return std::make_unique<Pipeline>(std::move(algos), std::move(pool));
}

}  // namespace compressor::processor

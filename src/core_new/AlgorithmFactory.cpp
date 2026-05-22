#include "AlgorithmFactory.hpp"

#include <memory>

#include "StreamingAdapter.hpp"

// algorithm_new pipeline functions (via ../algorithm_new/include)
#include "LZDP.hpp"
#include "Dpflate.hpp"
#include "LZSS.hpp"

namespace compressor::core {

bool (*g_cancel_callback)() = nullptr;

auto createAlgorithm(AlgorithmID id,
                     uint32_t file_compress_opts,
                     const LzdpWholeFileParams* lzdp_whole_file,
                     std::size_t streaming_compress_chunk_bytes,
                     const DpflatePipelineParams* dpflate_pipeline,
                     const DeflatePipelineParams* deflate_pipeline)
    -> std::unique_ptr<algorithm::IAlgorithm> {
    using SCA = StreamingCompressAdapter;
    using SDA = StreamingDecompressAdapter;
    const size_t sca_chunk =
        effective_stream_chunk_bytes(streaming_compress_chunk_bytes);

    switch (id) {
        // ──── algorithm_new (brick architecture) via SCA/SDA ────
        case AlgorithmID::LZDP_New: {
            const LzdpWholeFileParams wf_fallback{};
            const LzdpWholeFileParams& wf = lzdp_whole_file ? *lzdp_whole_file : wf_fallback;
            auto cfg = algorithm::LZDPConfig(
                wf.search_size, wf.lookahead_size, wf.dp_top,
                static_cast<algorithm::models::MatchEngine>(wf.match_engine),
                wf.use_flag_encoding, wf.min_match);
            return std::make_unique<SCA>(
                [cfg](const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
                    auto r = algorithm::pipeline::compress_bytes(data, cfg);
                    return r.compressed;
                },
                static_cast<std::size_t>(sca_chunk));
        }
        case AlgorithmID::LZDPDecompress_New: {
            const LzdpWholeFileParams wf_fallback{};
            const LzdpWholeFileParams& wf = lzdp_whole_file ? *lzdp_whole_file : wf_fallback;
            auto cfg = algorithm::LZDPConfig(
                wf.search_size, wf.lookahead_size, wf.dp_top,
                static_cast<algorithm::models::MatchEngine>(wf.match_engine),
                wf.use_flag_encoding, wf.min_match);
            return std::make_unique<SDA>(
                [cfg](const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
                    return algorithm::pipeline::decompress_bytes(data, cfg);
                });
        }
        case AlgorithmID::DPFlate_New: {
            const DpflatePipelineParams df_fallback{};
            const DpflatePipelineParams& df =
                dpflate_pipeline ? *dpflate_pipeline : df_fallback;
            auto cfg = algorithm::DPflateConfig(
                df.search_size, df.lookahead_size, df.max_chain_length,
                static_cast<algorithm::models::MatchEngine>(df.match_engine),
                df.use_flag_encoding, df.use_3hfmtree,
                static_cast<uint8_t>(df.huffman_offset_chunk_bits),
                static_cast<uint8_t>(df.huffman_length_chunk_bits),
                df.min_match);
            return std::make_unique<SCA>(
                [cfg](const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
                    auto r = algorithm::pipeline::compress_bytes_dpflate(data, cfg);
                    return r.compressed;
                },
                static_cast<std::size_t>(sca_chunk));
        }
        case AlgorithmID::LZSS_New: {
            const LzdpWholeFileParams wf_fallback{};
            const LzdpWholeFileParams& wf = lzdp_whole_file ? *lzdp_whole_file : wf_fallback;
            auto cfg = algorithm::LZSSConfig(
                wf.search_size, wf.lookahead_size,
                wf.min_match == 0 ? 3 : wf.min_match,
                wf.use_flag_encoding);
            return std::make_unique<SCA>(
                [cfg](const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
                    auto r = algorithm::pipeline::compress_bytes_lzss(data, cfg);
                    return r.compressed;
                },
                static_cast<std::size_t>(sca_chunk));
        }
        case AlgorithmID::LZSSDecompress_New: {
            const LzdpWholeFileParams wf_fallback{};
            const LzdpWholeFileParams& wf = lzdp_whole_file ? *lzdp_whole_file : wf_fallback;
            auto cfg = algorithm::LZSSConfig(
                wf.search_size, wf.lookahead_size,
                wf.min_match == 0 ? 3 : wf.min_match,
                wf.use_flag_encoding);
            return std::make_unique<SDA>(
                [cfg](const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
                    return algorithm::pipeline::decompress_bytes_lzss(data, cfg);
                });
        }
        default:
            break;
    }
    return nullptr;
}

}  // namespace compressor::core

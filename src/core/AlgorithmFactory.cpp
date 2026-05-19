#include "AlgorithmFactory.hpp"

#include <memory>

#include "Brotli.hpp"
#include "Deflate.hpp"
#include "Delta.hpp"
#include "Inflate.hpp"
#include "LZDP.hpp"
#include "LZSS.hpp"
#include "DPFlate.hpp"
#include "DPFlateBin64kDebug.hpp"
#include "ChunkedStreamAdapter.hpp"
#include "StreamChunkPolicy.hpp"
#include "Zstd.hpp"

namespace compressor::core {

bool (*g_cancel_callback)() = nullptr;

auto createAlgorithm(AlgorithmID id,
                     uint32_t file_compress_opts,
                     const LzdpWholeFileParams* lzdp_whole_file,
                     std::size_t streaming_compress_chunk_bytes,
                     const DpflatePipelineParams* dpflate_pipeline,
                     const DeflatePipelineParams* deflate_pipeline)
    -> std::unique_ptr<algorithm::IAlgorithm> {
    // Pipeline adapters from ChunkedStreamAdapter.hpp; short names keep the switch readable.
    using SCA = processor::StreamingCompressAdapter;          // SCA: per-chunk compress + u32 length framing
    using SDA = processor::StreamingDecompressAdapter;        // SDA: framed chunk decompress
    const size_t sca_chunk =
        processor::effective_stream_chunk_bytes(streaming_compress_chunk_bytes);

    switch (id) {
        case AlgorithmID::None:
            return nullptr;
        // File streaming: same ``AlgorithmBase`` push/pull model as ``DPFlate`` / ``LZDP_Streaming``
        // (no ``StreamingCompressAdapter`` / u32 framing). Bitstream is one continuous classic Deflate
        // stream inverted by ``Inflate`` (also unwrapped — no ``StreamingDecompressAdapter``).
        case AlgorithmID::Deflate: {
            const DeflatePipelineParams df_fallback{};
            const DeflatePipelineParams& dp =
                deflate_pipeline ? *deflate_pipeline : df_fallback;
            const std::size_t min_m =
                dp.min_match == 0 ? std::size_t{3} : dp.min_match;
            const std::size_t look =
                dp.lookahead_size == 0 ? std::size_t{258} : dp.lookahead_size;
            return std::make_unique<algorithm::Deflate>(
                dp.search_size, min_m, dp.max_chain_length, look);
        }
        case AlgorithmID::Inflate:
            return std::make_unique<algorithm::Inflate>();
        case AlgorithmID::DeltaEncode:
            return std::make_unique<algorithm::DeltaEncode>();
        case AlgorithmID::DeltaDecode:
            return std::make_unique<algorithm::DeltaDecode>();
        case AlgorithmID::DPFlate: {
            const DpflatePipelineParams df_fallback{};
            const DpflatePipelineParams& df =
                dpflate_pipeline ? *dpflate_pipeline : df_fallback;
            const std::size_t min_m =
                df.min_match == 0 ? std::size_t{4} : df.min_match;
            auto inst = std::make_unique<algorithm::DPFlate>(
                df.search_size, df.lookahead_size, min_m, df.max_chain_length,
                df.dp_sub_match_max);
            inst->set_match_engine(df.match_engine);
            inst->set_use_flag_encoding(df.use_flag_encoding);
            inst->set_use_3hfmtree(df.use_3hfmtree);
            const std::size_t hob =
                df.huffman_offset_chunk_bits > 0 ? df.huffman_offset_chunk_bits : 8;
            const std::size_t hlb =
                df.huffman_length_chunk_bits > 0 ? df.huffman_length_chunk_bits : 8;
            inst->set_huffman_offset_chunk_bits(hob);
            inst->set_huffman_length_chunk_bits(hlb);
            if (df.use_3hfmtree) {
                if (const char* only = std::getenv("WEBCOMPRESS_DPFLATE_BIN64K_ONLY");
                    only && only[0] == '1') {
                    algorithm::DPFlateBin64kDebug::arm_session(64u * 1024u, true);
                }
            }
            return inst;
        }
        case AlgorithmID::LZSS:
            return std::make_unique<SCA>(
                [](const std::vector<uint8_t>& data) {
                    return algorithm::LZSS::compress(data);
                },
                sca_chunk);
        case AlgorithmID::LZSSDecompress:
            return std::make_unique<SDA>(
                [](const std::vector<uint8_t>& data) {
                    return algorithm::LZSS::decompress(data);
                });
        case AlgorithmID::LZSS_NoFlag:
            return std::make_unique<SCA>(
                [](const std::vector<uint8_t>& data) {
                    return algorithm::LZSS::compress(data, 4096, 3, false);
                },
                sca_chunk);
        case AlgorithmID::LZSSDecompress_NoFlag:
            return std::make_unique<SDA>(
                [](const std::vector<uint8_t>& data) {
                    return algorithm::LZSS::decompress(data, 3, false);
                });
        case AlgorithmID::LZDP: {
            // Streaming path: ``LZDP_Streaming`` — chunked plaintext window, forward DP with packed
            // link spill (temp A), backtrack to temp B, then emit bitstream (see
            // ``docs/design/lzdp-file-pipeline-design.md``). Parameters from ``LzdpWholeFileParams``.
            const LzdpWholeFileParams wf_fallback{};
            const LzdpWholeFileParams wf = lzdp_whole_file ? *lzdp_whole_file : wf_fallback;
            return std::make_unique<algorithm::LZDP_Streaming>(
                wf.search_size, wf.lookahead_size, wf.min_match, wf.dp_top,
                wf.use_flag_encoding, wf.match_engine);
        }
        case AlgorithmID::LZDPDecompress:
            return std::make_unique<algorithm::LZDPDecompress_Streaming>(false);
        case AlgorithmID::Brotli:
            return std::make_unique<SCA>(
                [](const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
                    return algorithm::brotli_encode(
                        data, algorithm::BrotliParams{65536, 3, 256});
                },
                sca_chunk);
        case AlgorithmID::BrotliDecompress:
            return std::make_unique<SDA>(
                [](const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
                    return algorithm::brotli_decode(data);
                });
        case AlgorithmID::Zstd:
            return std::make_unique<SCA>(
                [](const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
                    return algorithm::ZstdCompress::compress(data, 3);
                },
                sca_chunk);
        case AlgorithmID::ZstdDecompress:
            return std::make_unique<SDA>(
                [](const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
                    algorithm::ZstdDecompress decomp;
                    return decomp.decompress(data);
                });
    }
    return nullptr;
}

}  // namespace compressor::core

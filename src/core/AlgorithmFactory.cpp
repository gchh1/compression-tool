#include "AlgorithmFactory.hpp"

#include <memory>

#include "Brotli.hpp"
#include "Deflate.hpp"
#include "Delta.hpp"
#include "ImageCompressor.hpp"
#include "Inflate.hpp"
#include "LZDP.hpp"
#include "LZSS.hpp"
#include "DPFlate.hpp"
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
                     const DeflatePipelineParams* deflate_pipeline,
                     const ImageCompressParams* image_compress)
    -> std::unique_ptr<algorithm::IAlgorithm> {
    using SDA = processor::StreamingDecompressAdapter;        // SDA: framed chunk decompress
    const size_t sca_chunk =
        processor::effective_stream_chunk_bytes(streaming_compress_chunk_bytes);

    switch (id) {
        case AlgorithmID::None:
            return nullptr;
        // File streaming: same ``AlgorithmBase`` push/pull model as ``DPFlate`` / ``LZDP_OutOfCore``
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
            return inst;
        }
        case AlgorithmID::LZSS:
            return std::make_unique<algorithm::LZSS_OutOfCore>();
        case AlgorithmID::LZSSDecompress:
            return std::make_unique<SDA>(
                [](const std::vector<uint8_t>& data) {
                    return algorithm::LZSS::decompress(data);
                });
        case AlgorithmID::LZSS_NoFlag:
            return std::make_unique<algorithm::LZSS_OutOfCore>(4096, 3, false);
        case AlgorithmID::LZSSDecompress_NoFlag:
            return std::make_unique<SDA>(
                [](const std::vector<uint8_t>& data) {
                    return algorithm::LZSS::decompress(data, 3, false);
                });
        case AlgorithmID::LZDP: {
            // Streaming path: ``LZDP_OutOfCore`` — chunked plaintext window, forward DP with packed
            // link spill (temp A), backtrack to temp B, then emit bitstream (see
            // ``docs/design/lzdp-file-pipeline-design.md``). Parameters from ``LzdpWholeFileParams``.
            const LzdpWholeFileParams wf_fallback{};
            const LzdpWholeFileParams wf = lzdp_whole_file ? *lzdp_whole_file : wf_fallback;
            return std::make_unique<algorithm::LZDP_OutOfCore>(
                wf.search_size, wf.lookahead_size, wf.min_match, wf.dp_top,
                wf.use_flag_encoding, wf.match_engine);
        }
        case AlgorithmID::LZDPDecompress:
            return std::make_unique<algorithm::LZDPDecompress_OutOfCore>(false);
        case AlgorithmID::Brotli:
            return std::make_unique<algorithm::BrotliCompress>(65536, 3, 256);
        case AlgorithmID::BrotliDecompress:
            return std::make_unique<SDA>(
                [](const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
                    algorithm::BrotliDecompress decompress;
                    std::vector<uint8_t> out(std::max(data.size() * 4 + 65536, size_t(2097152)));
                    auto status = decompress.process(data, out, true);
                    out.resize(status.bytes_produced);
                    return out;
                });
        case AlgorithmID::Zstd:
            return std::make_unique<algorithm::ZstdCompress>(3);
        case AlgorithmID::ZstdDecompress:
            return std::make_unique<SDA>(
                [](const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
                    algorithm::ZstdDecompress decomp;
                    return decomp.decompress(data);
                });
        case AlgorithmID::JPEG_Compress: {
            auto q = image_compress ? image_compress->quality : 85;
            auto mw = image_compress ? image_compress->max_width : 0;
            auto mh = image_compress ? image_compress->max_height : 0;
            return std::make_unique<algorithm::ImageCompressor>(
                algorithm::ImageFormat::JPEG, q, mw, mh);
        }
        case AlgorithmID::JPEG_Decompress:
            return nullptr;   // passthrough
        case AlgorithmID::WebP_Compress: {
            auto q = image_compress ? image_compress->quality : 80;
            auto mw = image_compress ? image_compress->max_width : 0;
            auto mh = image_compress ? image_compress->max_height : 0;
            return std::make_unique<algorithm::ImageCompressor>(
                algorithm::ImageFormat::JPEG, q, mw, mh);  // fallback JPEG until libwebp
        }
        case AlgorithmID::WebP_Decompress:
            return nullptr;   // passthrough
    }
    return nullptr;
}

}  // namespace compressor::core

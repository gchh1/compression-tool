#pragma once

#include <cstddef>
#include <memory>
#include <unordered_map>

#include "IAlgorithm.hpp"

namespace compressor::core {

/// Snapshot for ``LZDP_OutOfCore`` when using whole-file framed file compression
/// (must match GUI ``pipeline_compress`` defaults when unset).
struct LzdpWholeFileParams {
    std::size_t search_size{4096};
    std::size_t lookahead_size{256};
    std::size_t min_match{0};
    std::size_t dp_top{3};
    bool use_flag_encoding{false};
    int match_engine{0};
};

/// Snapshot for file-streaming ``algorithm::Deflate`` (same window fields as ``DpflatePipelineParams``
/// / GUI: ``search_size``, ``lookahead_size``, ``min_match``, ``max_chain_length``).
struct DeflatePipelineParams {
    std::size_t search_size{4096};
    std::size_t lookahead_size{256};
    std::size_t min_match{0};
    std::size_t max_chain_length{256};
};

/// Snapshot for file-streaming ``algorithm::DPFlate`` (must match GUI).
struct DpflatePipelineParams {
    std::size_t search_size{4096};
    std::size_t lookahead_size{256};
    std::size_t min_match{0};
    /// Passed as ``dp_top`` to ``algorithm::DPFlate`` ctor.
    std::size_t max_chain_length{256};
    std::size_t dp_sub_match_max{6};
    int match_engine{1};
    bool use_flag_encoding{false};
    bool use_3hfmtree{false};
    /// 3HfM：offset / length 两套槽宽（bit）；仅 DPFlate 流式/内存管线使用。
    std::size_t huffman_offset_chunk_bits{8};
    std::size_t huffman_length_chunk_bits{8};
};

/// Options for ``createAlgorithm`` when used from file streaming compress path.
inline constexpr uint32_t kFileCompressOptsNone = 0;
/// LZDP: legacy name — file pipeline uses ``LZDP_OutOfCore`` + ``LzdpWholeFileParams``. Bit kept for ABI / logs.
inline constexpr uint32_t kFileCompressLzdpWholeFileFramed = 1u << 0;

enum class AlgorithmID {
    None,
    Deflate,
    Inflate,
    DeltaEncode,
    DeltaDecode,
    LZSS,
    LZSSDecompress,
    LZSS_NoFlag,
    LZSSDecompress_NoFlag,
    LZDP,
    LZDPDecompress,
    DPFlate,
    Brotli,
    BrotliDecompress,
    Zstd,
    ZstdDecompress,
};

/// @param streaming_compress_chunk_bytes  Raw requested plaintext chunk size (bytes). Always
///                                        normalized via ``processor::effective_stream_chunk_bytes``
///                                        (same min/max/default for all file streaming). Used as
///                                        the ``StreamingCompressAdapter`` segment size for LZSS /
///                                        Brotli / Zstd, and as the file read / ``MemoryPool`` chunk
///                                        for ``algorithm::Deflate``, ``LZDP_OutOfCore``, and
///                                        ``DPFlate`` (one ``process()`` read per disk chunk).
/// @param deflate_pipeline                 When ``id == Deflate``, ctor args for ``algorithm::Deflate``
///                                        (omit or null for defaults). Ignored for other ids.
auto createAlgorithm(AlgorithmID id,
                       uint32_t file_compress_opts = kFileCompressOptsNone,
                       const LzdpWholeFileParams* lzdp_whole_file = nullptr,
                       std::size_t streaming_compress_chunk_bytes = 0,
                       const DpflatePipelineParams* dpflate_pipeline = nullptr,
                       const DeflatePipelineParams* deflate_pipeline = nullptr)
    -> std::unique_ptr<algorithm::IAlgorithm>;

extern bool (*g_cancel_callback)();

inline AlgorithmID getDecompressorID(AlgorithmID comp) {
    static const std::unordered_map<AlgorithmID, AlgorithmID> map = {
        {AlgorithmID::Deflate, AlgorithmID::Inflate},
        {AlgorithmID::LZSS, AlgorithmID::LZSSDecompress},
        {AlgorithmID::LZSS_NoFlag, AlgorithmID::LZSSDecompress_NoFlag},
        {AlgorithmID::LZDP, AlgorithmID::LZDPDecompress},
        /// DPFlate emits a Deflate-style Huffman bitstream; ``Inflate`` inverts it
        /// (see ``DPFlate.hpp`` ``using DPFlateDecompress = Inflate``).
        {AlgorithmID::DPFlate, AlgorithmID::Inflate},
        {AlgorithmID::Brotli, AlgorithmID::BrotliDecompress},
        {AlgorithmID::Zstd, AlgorithmID::ZstdDecompress},
    };
    auto it = map.find(comp);
    return it != map.end() ? it->second : AlgorithmID::None;
}

inline AlgorithmID getPostpressorID(AlgorithmID pre) {
    static const std::unordered_map<AlgorithmID, AlgorithmID> map = {
        {AlgorithmID::None, AlgorithmID::None},
        {AlgorithmID::DeltaEncode, AlgorithmID::DeltaDecode},
    };
    auto it = map.find(pre);
    return it != map.end() ? it->second : AlgorithmID::None;
}

}

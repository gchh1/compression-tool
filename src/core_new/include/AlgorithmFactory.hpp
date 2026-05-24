#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace compressor::core {

struct LzdpWholeFileParams {
    std::size_t search_size{4096};
    std::size_t lookahead_size{256};
    std::size_t min_match{0};
    std::size_t dp_top{3};
    bool use_flag_encoding{false};
    int match_engine{0};
};

struct LzssPipelineParams {
    std::size_t search_size{4095};
    std::size_t lookahead_size{255};
    std::size_t min_match{0};
    bool use_flag_encoding{true};
};

struct DeflatePipelineParams {
    std::size_t search_size{4096};
    std::size_t lookahead_size{256};
    std::size_t min_match{0};
    std::size_t max_chain_length{256};
    bool use_flag_encoding{true};
    bool use_3hfmtree{false};
    std::size_t huffman_offset_chunk_bits{8};
    std::size_t huffman_length_chunk_bits{8};
};

struct DpflatePipelineParams {
    std::size_t search_size{4096};
    std::size_t lookahead_size{256};
    std::size_t min_match{0};
    std::size_t max_chain_length{256};
    std::size_t dp_sub_match_max{6};
    int match_engine{1};
    bool use_flag_encoding{false};
    bool use_3hfmtree{false};
    std::size_t huffman_offset_chunk_bits{8};
    std::size_t huffman_length_chunk_bits{8};
};

inline constexpr uint32_t kFileCompressOptsNone = 0;
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
    ImageJpeg,
    ImagePng,
    ImageJpegDecompress,
    ImagePngDecompress,
    LZDP_New,
    LZDPDecompress_New,
    DPFlate_New,
    Deflate_New,
    DeflateDecompress_New,
    LZSS_New,
    LZSSDecompress_New,
};

inline AlgorithmID getDecompressorID(AlgorithmID comp) {
    static const std::unordered_map<AlgorithmID, AlgorithmID> map = {
        {AlgorithmID::Deflate, AlgorithmID::Inflate},
        {AlgorithmID::LZSS, AlgorithmID::LZSSDecompress},
        {AlgorithmID::LZSS_NoFlag, AlgorithmID::LZSSDecompress_NoFlag},
        {AlgorithmID::LZDP, AlgorithmID::LZDPDecompress},
        {AlgorithmID::DPFlate, AlgorithmID::Inflate},
        {AlgorithmID::Brotli, AlgorithmID::BrotliDecompress},
        {AlgorithmID::Zstd, AlgorithmID::ZstdDecompress},
        {AlgorithmID::ImageJpeg, AlgorithmID::ImageJpegDecompress},
        {AlgorithmID::ImagePng, AlgorithmID::ImagePngDecompress},
        {AlgorithmID::LZDP_New, AlgorithmID::LZDPDecompress_New},
        {AlgorithmID::DPFlate_New, AlgorithmID::Inflate},
        {AlgorithmID::Deflate_New, AlgorithmID::Inflate},
        {AlgorithmID::DeflateDecompress_New, AlgorithmID::Inflate},
        {AlgorithmID::LZSS_New, AlgorithmID::LZSSDecompress_New},
    };
    auto it = map.find(comp);
    return it != map.end() ? it->second : AlgorithmID::None;
}

}  // namespace compressor::core

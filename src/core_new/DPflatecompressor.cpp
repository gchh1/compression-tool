#include "DPflatecompressor.hpp"

#include "Huffman_3HfMTree.hpp"
#include "Huffman_Inflate.hpp"
#include "io/FileIO.hpp"

namespace compressor::core_new {

DPflateCompressor::DPflateCompressor(DPflateCompressorConfig config)
    : config_(std::move(config)) {}

std::vector<uint8_t> DPflateCompressor::compress_file(const std::string& input_path) {
    if (config_.use_streaming) {
        const auto tmp_out = config_.workspace_dir + "/_dpflate_stream_out.bin";
        compress_file_to_path(input_path, tmp_out);
        return io::read_file_bytes(tmp_out);
    }

    const auto input = io::read_file_bytes(input_path);
    auto lzdp_cfg = config_.dpflate.to_lzdp_config();

    algorithm::LZDP lzdp(lzdp_cfg);
    std::vector<algorithm::models::DPNode> dp(input.size() + 1,
        algorithm::models::DPNode{});
    if (!input.empty()) {
        dp[0] = algorithm::models::DPNode(0, 0, -1,
            algorithm::Triple(0, 0, 0));
    }

    algorithm::VectorByteInput view{input, 0};
    lzdp.dpforward(view, dp, 0, input.size());

    int cur_pos = 0;
    auto triples = lzdp.dpbacktrack(dp, cur_pos, 0);

    std::vector<uint8_t> encoded;
    if (config_.dpflate.huffman.use_3hfmtree) {
        algorithm::Huffman_3HfMTConfig hmcfg{
            config_.dpflate.huffman.huffman_offset_bitwidth,
            config_.dpflate.huffman.huffman_length_bitwidth};
        algorithm::Huffman_3HfMT encoder(hmcfg, config_.dpflate.encoding);
        encoder.countFreq(triples);
        encoder.buildTree();
        encoded = encoder.encode(triples);
    } else {
        algorithm::Huffman_InflateConfig icfg{
            config_.dpflate.encoding.offset_bits,
            config_.dpflate.encoding.length_bits,
            config_.dpflate.huffman.huffman_offset_bitwidth,
            config_.dpflate.huffman.huffman_length_bitwidth};
        algorithm::Huffman_Inflate encoder(icfg, config_.dpflate.encoding);
        encoder.countFreq(triples);
        encoder.buildTree();
        encoded = encoder.encode(triples);
    }

    return encoded;
}

void DPflateCompressor::compress_file_to_path(const std::string& input_path,
                                               const std::string& output_path) {
    if (!config_.use_streaming) {
        auto result = compress_file(input_path);
        io::write_file_bytes(output_path, result);
        return;
    }

    algorithm::pipeline::DPFlateStreamingOptions opts;
    opts.chunk_size = config_.streaming_chunk_size;
    opts.workspace_dir = config_.workspace_dir;

    algorithm::pipeline::DPFlateStreamingPipeline pipe(config_.dpflate, opts);
    pipe.compress_file(input_path, output_path);
}

std::vector<uint8_t> DPflateCompressor::decompress_file(
    const std::string& compressed_path) {
    auto compressed = io::read_file_bytes(compressed_path);

    std::vector<algorithm::Triple> triples;
    if (config_.dpflate.huffman.use_3hfmtree) {
        algorithm::Huffman_3HfMTConfig hmcfg{
            config_.dpflate.huffman.huffman_offset_bitwidth,
            config_.dpflate.huffman.huffman_length_bitwidth};
        algorithm::Huffman_3HfMT decoder(hmcfg, config_.dpflate.encoding);
        // NOTE: tree must be built before decoding from a pre-built tree
        // For now, decoder needs tree to be serialized in the bitstream
        triples = decoder.decode(compressed);
    } else {
        algorithm::Huffman_InflateConfig icfg{
            config_.dpflate.encoding.offset_bits,
            config_.dpflate.encoding.length_bits,
            config_.dpflate.huffman.huffman_offset_bitwidth,
            config_.dpflate.huffman.huffman_length_bitwidth};
        algorithm::Huffman_Inflate decoder(icfg, config_.dpflate.encoding);
        triples = decoder.decode(compressed);
    }

    compressor::utils::_buffer pending;
    return algorithm::decode_triple(triples, config_.dpflate.encoding, pending);
}

}  // namespace compressor::core_new
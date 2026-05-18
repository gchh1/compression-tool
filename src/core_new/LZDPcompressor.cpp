#include "LZDPcompressor.hpp"

#include "io/FileIO.hpp"

namespace compressor::core_new {

LZDPCompressor::LZDPCompressor(LZDPCompressorConfig config)
    : config_(std::move(config)) {}

std::vector<uint8_t> LZDPCompressor::compress_file(const std::string& input_path) {
    if (config_.use_streaming) {
        const auto tmp_out = config_.workspace_dir + "/_lzdp_stream_out.bin";
        compress_file_to_path(input_path, tmp_out);
        return io::read_file_bytes(tmp_out);
    }
    const auto input = io::read_file_bytes(input_path);
    auto result = algorithm::pipeline::compress_bytes(input, config_.lzdp);
    return result.compressed;
}

void LZDPCompressor::compress_file_to_path(const std::string& input_path,
                                             const std::string& output_path) {
    if (!config_.use_streaming) {
        const auto input = io::read_file_bytes(input_path);
        auto result = algorithm::pipeline::compress_bytes(input, config_.lzdp);
        io::write_file_bytes(output_path, result.compressed);
        return;
    }

    algorithm::pipeline::LZDPStreamingOptions opts;
    opts.chunk_size = config_.streaming_chunk_size;
    opts.workspace_dir = config_.workspace_dir;

    algorithm::pipeline::LZDPStreamingPipeline pipe(config_.lzdp, opts);
    pipe.compress_file(input_path, output_path);
}

std::vector<uint8_t> LZDPCompressor::decompress_file(const std::string& compressed_path) {
    const auto compressed = io::read_file_bytes(compressed_path);
    return algorithm::pipeline::decompress_bytes(compressed, config_.lzdp);
}

}  // namespace compressor::core_new

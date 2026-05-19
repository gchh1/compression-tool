#include "Deflatecompressor.hpp"

#include <fstream>

#include "Huffman_3HfMTree.hpp"
#include "Huffman_Inflate.hpp"
#include "io/FileIO.hpp"
#include "Streaming.hpp"

namespace compressor::core_new {

DeflateCompressor::DeflateCompressor(DeflateCompressorConfig config)
    : config_(std::move(config)) {}

std::vector<uint8_t> DeflateCompressor::compress_file(const std::string& input_path) {
    if (config_.use_streaming) {
        const auto tmp_out = config_.workspace_dir + "/_deflate_stream_out.bin";
        compress_file_to_path(input_path, tmp_out);
        return io::read_file_bytes(tmp_out);
    }
    const auto input = io::read_file_bytes(input_path);
    auto result = algorithm::pipeline::compress_bytes_deflate(input, config_.deflate);
    return result.compressed;
}

void DeflateCompressor::compress_file_to_path(const std::string& input_path,
                                                const std::string& output_path) {
    if (!config_.use_streaming) {
        const auto input = io::read_file_bytes(input_path);
        auto result = algorithm::pipeline::compress_bytes_deflate(input, config_.deflate);
        io::write_file_bytes(output_path, result.compressed);
        return;
    }

    algorithm::pipeline::DeflateStreamingOptions opts;
    opts.chunk_size = config_.streaming_chunk_size;
    opts.workspace_dir = config_.workspace_dir;

    algorithm::pipeline::DeflateStreamingPipeline pipe(config_.deflate, opts);
    pipe.compress_file(input_path, output_path);
}

std::vector<uint8_t> DeflateCompressor::decompress_file(const std::string& compressed_path) {
    const auto compressed = io::read_file_bytes(compressed_path);
    return algorithm::pipeline::decompress_bytes_deflate(compressed, config_.deflate);
}

void DeflateCompressor::decompress_file_to_path(const std::string& compressed_path,
                                                  const std::string& output_path,
                                                  size_t write_chunk_size) {
    const auto compressed = io::read_file_bytes(compressed_path);
    auto decompressed = algorithm::pipeline::decompress_bytes_deflate(compressed, config_.deflate);

    algorithm::streaming::File_Chunk_Writer writer(output_path);
    size_t cursor = 0;
    while (cursor < decompressed.size()) {
        size_t batch = std::min(write_chunk_size, decompressed.size() - cursor);
        std::vector<uint8_t> chunk(decompressed.begin() + static_cast<std::ptrdiff_t>(cursor),
                                   decompressed.begin() + static_cast<std::ptrdiff_t>(cursor + batch));
        writer.write_chunk(chunk);
        cursor += batch;
    }
}

}  // namespace compressor::core_new
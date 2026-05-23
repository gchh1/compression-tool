#include "Deflatecompressor.hpp"

#include <fstream>

#include "io/FileIO.hpp"
#include "StreamingCancel.hpp"

namespace compressor::core_new {

DeflateCompressor::DeflateCompressor(DeflateCompressorConfig config)
    : config_(std::move(config)) {}

std::vector<uint8_t> DeflateCompressor::compress_file(const std::string& input_path) {
    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }
    const auto input = io::read_file_bytes(input_path);
    auto result = algorithm::deflate_compress(input, config_.deflate);
    return result.compressed;
}

void DeflateCompressor::compress_file_to_path(const std::string& input_path,
                                                const std::string& output_path) {
    const auto input = io::read_file_bytes(input_path);
    auto result = algorithm::deflate_compress(input, config_.deflate);
    io::write_file_bytes(output_path, result.compressed);
}

std::vector<uint8_t> DeflateCompressor::decompress_file(const std::string& compressed_path) {
    const auto compressed = io::read_file_bytes(compressed_path);
    return algorithm::deflate_decompress(compressed, config_.deflate);
}

void DeflateCompressor::decompress_file_to_path(const std::string& compressed_path,
                                                  const std::string& output_path,
                                                  size_t write_chunk_size) {
    const auto compressed = io::read_file_bytes(compressed_path);
    auto decompressed = algorithm::deflate_decompress(compressed, config_.deflate);

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
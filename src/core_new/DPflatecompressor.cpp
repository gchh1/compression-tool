#include "DPflatecompressor.hpp"

#include "io/FileIO.hpp"
#include "StreamingCancel.hpp"

namespace compressor::core_new {

DPflateCompressor::DPflateCompressor(DPflateCompressorConfig config)
    : config_(std::move(config)) {}

std::vector<uint8_t> DPflateCompressor::compress_file(const std::string& input_path) {
    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }
    const auto input = io::read_file_bytes(input_path);
    auto result = algorithm::dpflate_compress(input, config_.dpflate);
    return result.compressed;
}

void DPflateCompressor::compress_file_to_path(const std::string& input_path,
                                               const std::string& output_path) {
    const auto input = io::read_file_bytes(input_path);
    auto result = algorithm::dpflate_compress(input, config_.dpflate);
    io::write_file_bytes(output_path, result.compressed);
}

std::vector<uint8_t> DPflateCompressor::decompress_file(
    const std::string& compressed_path) {
    auto compressed = io::read_file_bytes(compressed_path);
    return algorithm::dpflate_decompress(compressed, config_.dpflate);
}

}  // namespace compressor::core_new
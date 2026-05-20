#include "LZSS.hpp"

#include <filesystem>
#include <utility>

#include "EncodingTriple.hpp"
#include "RecordIO.hpp"
#include "Streaming.hpp"

namespace compressor::algorithm::pipeline {

namespace fs = std::filesystem;

// ──── Non-streaming ────

LZSSNonStreamingResult compress_bytes_lzss(
    const std::vector<uint8_t>& input,
    const LZSSConfig& config) {
    LZSSNonStreamingResult result;
    if (input.empty()) return result;

    LZSS lzss(config);
    auto triples = lzss.greedyMatch(input);

    if (!config.encoding.use_flag_encoding) {
        triples = literalrun(triples, config.window.look_size);
    }

    compressor::utils::_buffer pending;
    result.triples = std::move(triples);
    result.compressed =
        encoding_triple_lz(result.triples, config.encoding, pending, true);
    return result;
}

std::vector<uint8_t> decompress_bytes_lzss(
    const std::vector<uint8_t>& compressed,
    const LZSSConfig& config) {
    compressor::utils::_buffer pending;
    auto triples = readtriple(compressed, config.encoding, pending);
    return decode_triple(triples, config.encoding, pending);
}

// ──── Streaming ────

LZSSStreamingPipeline::LZSSStreamingPipeline(LZSSConfig config, LZSSStreamingOptions options)
    : config_(std::move(config)), options_(std::move(options)), lzss_(config_) {}

std::string LZSSStreamingPipeline::temp_a_path() const {
    return (fs::path(options_.workspace_dir) / options_.temp_a_name).string();
}

void LZSSStreamingPipeline::compress_file(const std::string& input_path,
                                           const std::string& output_path) {
    fs::create_directories(options_.workspace_dir);

    std::vector<uint8_t> full_input;
    {
        streaming::File_Chunk_Reader reader(input_path, options_.chunk_size);
        while (!reader.is_end()) {
            auto chunk = reader.read_chunk();
            if (!chunk.empty()) {
                full_input.insert(full_input.end(), chunk.begin(), chunk.end());
            }
        }
    }

    auto triples = lzss_.greedyMatch(full_input);
    if (!config_.encoding.use_flag_encoding) {
        triples = literalrun(triples, config_.window.look_size);
    }

    full_input.clear();

    // Single bitstream with one final flush — must match compress_bytes_lzss (L1/L3 条件 A).
    compressor::utils::_buffer pending;
    const auto compressed =
        encoding_triple_lz(triples, config_.encoding, pending, true);

    streaming::File_Chunk_Writer out_writer(output_path);
    if (!compressed.empty()) {
        out_writer.write_chunk(compressed);
    }
}

}  // namespace compressor::algorithm::pipeline
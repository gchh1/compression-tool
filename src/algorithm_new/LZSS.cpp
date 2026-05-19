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

    {
        streaming::File_Chunk_Writer temp_writer(temp_a_path());
        compressor::utils::_buffer pending;
        size_t cursor = 0;
        const size_t chunk_estimate = options_.chunk_size / record_io::kTripleRecordBytes;
        while (cursor < triples.size()) {
            size_t batch = std::min(chunk_estimate, triples.size() - cursor);
            std::vector<Triple> slice(triples.begin() + static_cast<std::ptrdiff_t>(cursor),
                                      triples.begin() + static_cast<std::ptrdiff_t>(cursor + batch));
            auto encoded = record_io::triples_to_u8(slice, pending);
            if (!encoded.empty()) {
                temp_writer.write_chunk(encoded);
            }
            cursor += batch;
        }
        if (pending.count > 0) {
            const int pad = 8 - pending.count;
            pending.buf <<= pad;
            std::vector<uint8_t> last{static_cast<uint8_t>(pending.buf & 0xFFu)};
            temp_writer.write_chunk(last);
        }
    }

    {
        streaming::File_Chunk_Reader temp_reader(temp_a_path(), options_.chunk_size);
        streaming::File_Chunk_Writer out_writer(output_path);
        std::vector<uint8_t> triple_carry;
        compressor::utils::_buffer emit_pending;

        while (!temp_reader.is_end()) {
            auto chunk = temp_reader.read_chunk();
            auto parsed = record_io::u8_to_triples(chunk, triple_carry, emit_pending);
            auto tr = std::move(parsed.first);
            if (tr.empty()) continue;
            out_writer.write_chunk(
                encoding_triple_lz(tr, config_.encoding, emit_pending, false));
        }

        if (!triple_carry.empty()) {
            std::vector<uint8_t> empty_chunk;
            auto parsed = record_io::u8_to_triples(empty_chunk, triple_carry, emit_pending);
            auto tr = parsed.first;
            if (!tr.empty()) {
                out_writer.write_chunk(
                    encoding_triple_lz(tr, config_.encoding, emit_pending, true));
            }
        } else {
            std::vector<Triple> empty;
            auto tail = encoding_triple_lz(empty, config_.encoding, emit_pending, true);
            if (!tail.empty()) {
                out_writer.write_chunk(tail);
            }
        }
    }
}

}  // namespace compressor::algorithm::pipeline
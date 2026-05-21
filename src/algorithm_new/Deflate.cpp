#include "Deflate.hpp"
#include "MatchEngine.hpp"

// ──── Pipeline ────

#include <filesystem>
#include <fstream>
#include <utility>

#include "EncodingTriple.hpp"
#include "RecordIO.hpp"
#include "Streaming.hpp"
#include "StreamingCancel.hpp"

namespace compressor::algorithm::pipeline {

namespace fs = std::filesystem;

DeflateNonStreamingResult compress_bytes_deflate(
    const std::vector<uint8_t>& input,
    const DeflateConfig& config) {
    DeflateNonStreamingResult result;
    if (input.empty()) return result;

    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }

    Deflate deflate(config);
    auto triples = LZMatcher::greedyWholeInput(
        input, config.window, config.encoding.offset_bits, config.encoding.length_bits);

    compressor::utils::_buffer pending;
    result.triples = triples;
    result.compressed = deflate.huffmanEncode(triples);
    (void)pending;
    return result;
}

std::vector<uint8_t> decompress_bytes_deflate(
    const std::vector<uint8_t>& compressed,
    const DeflateConfig& config) {
    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }

    Deflate deflate(config);
    auto triples = deflate.huffmanDecode(compressed);

    compressor::utils::_buffer pending;
    return decode_triple(triples, config.encoding, pending);
}

DeflateStreamingPipeline::DeflateStreamingPipeline(DeflateConfig config, DeflateStreamingOptions options)
    : config_(std::move(config)), options_(std::move(options)) {}

std::string DeflateStreamingPipeline::temp_a_path() const {
    return (fs::path(options_.workspace_dir) / options_.temp_a_name).string();
}

void DeflateStreamingPipeline::compress_file(const std::string& input_path,
                                               const std::string& output_path) {
    fs::create_directories(options_.workspace_dir);

    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }

    std::vector<uint8_t> full_input;
    {
        streaming::File_Chunk_Reader reader(input_path, options_.chunk_size);
        while (!reader.is_end()) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            auto chunk = reader.read_chunk();
            if (!chunk.empty()) {
                full_input.insert(full_input.end(), chunk.begin(), chunk.end());
            }
        }
    }

    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }

    auto triples = LZMatcher::greedyWholeInput(
        full_input,
        config_.window,
        config_.encoding.offset_bits,
        config_.encoding.length_bits);
    full_input.clear();

    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }

    {
        streaming::File_Chunk_Writer temp_writer(temp_a_path());
        compressor::utils::_buffer pending;
        size_t cursor = 0;
        const size_t chunk_estimate = options_.chunk_size / record_io::kTripleRecordBytes;
        while (cursor < triples.size()) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
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

    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }

    {
        streaming::File_Chunk_Reader temp_reader(temp_a_path(), options_.chunk_size);
        std::vector<uint8_t> triple_carry;
        compressor::utils::_buffer pending;
        std::vector<Triple> all_triples;

        while (!temp_reader.is_end()) {
            auto chunk = temp_reader.read_chunk();
            auto parsed = record_io::u8_to_triples(chunk, triple_carry, pending);
            auto tr = std::move(parsed.first);
            if (!tr.empty()) {
                all_triples.insert(all_triples.end(), tr.begin(), tr.end());
            }
        }
        if (!triple_carry.empty()) {
            std::vector<uint8_t> empty_chunk;
            auto parsed = record_io::u8_to_triples(empty_chunk, triple_carry, pending);
            auto tr = parsed.first;
            if (!tr.empty()) {
                all_triples.insert(all_triples.end(), tr.begin(), tr.end());
            }
        }

        if (core_new::is_streaming_cancel_requested()) {
            throw std::runtime_error("cancelled");
        }

        Deflate deflate(config_);
        auto encoded = deflate.huffmanEncode(all_triples);
        if (!encoded.empty()) {
            std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(encoded.data()),
                      static_cast<std::streamsize>(encoded.size()));
        }
    }
}

}  // namespace compressor::algorithm::pipeline
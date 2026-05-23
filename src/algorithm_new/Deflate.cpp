#include "Deflate.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <utility>

#include "Inflate3HMCoding.hpp"
#include "InflateCoding.hpp"
#include "MatchEngine.hpp"
#include "RecordIO.hpp"
#include "Streaming.hpp"
#include "StreamingCancel.hpp"

namespace compressor::algorithm {

DeflateNonStreamingResult compress_bytes_deflate(
    const std::vector<uint8_t>& input,
    const DeflateConfig& config) {
    DeflateNonStreamingResult result;
    if (input.empty()) return result;
    if (core_new::is_streaming_cancel_requested()) throw std::runtime_error("cancelled");

    auto triples = LZMatcher::greedyWholeInput(
        input, config.window,
        config.encoding.offset_bits,
        config.encoding.length_bits);

    if (!config.encoding.use_flag_encoding && !config.use_3hfmtree) {
        triples = literalrun(triples, config.window.look_size);
    }

    result.triples = triples;

    compressor::utils::_buffer pending;
    if (config.use_3hfmtree) {
        auto enc = inflate3hm_encode(triples, config.huffman_3hm, pending);
        result.compressed = std::move(enc.data);
    } else if (!config.encoding.use_flag_encoding) {
        result.compressed = encoding_triple_lz(triples, config.encoding, pending, true);
    } else {
        auto enc = inflate_encode(triples, pending);
        result.compressed = std::move(enc.data);
    }
    return result;
}

std::vector<uint8_t> decompress_bytes_deflate(
    const std::vector<uint8_t>& compressed,
    const DeflateConfig& config) {
    if (core_new::is_streaming_cancel_requested()) throw std::runtime_error("cancelled");
    compressor::utils::_buffer pending;
    if (config.use_3hfmtree) {
        auto dec = inflate3hm_decode(compressed, pending);
        return std::move(dec.data);
    }
    if (!config.encoding.use_flag_encoding) {
        auto triples = readtriple(compressed, config.encoding, pending);
        return decode_triple(triples, config.encoding, pending);
    }
    auto dec = inflate_decode(compressed, pending);
    return std::move(dec.data);
}

}  // namespace compressor::algorithm

namespace compressor::algorithm::pipeline {

namespace fs = std::filesystem;

void runDeflateMatchingPhase(
    const std::string& input_path,
    const std::string& temp_triples_path,
    const DeflateConfig& config,
    size_t chunk_size) {
    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }

    std::vector<uint8_t> input;
    {
        streaming::File_Chunk_Reader reader(input_path, chunk_size);
        while (!reader.is_end()) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            auto chunk = reader.read_chunk();
            input.insert(input.end(), chunk.begin(), chunk.end());
        }
    }

    auto triples = LZMatcher::greedyWholeInput(
        input, config.window,
        config.encoding.offset_bits,
        config.encoding.length_bits);

    {
        std::vector<uint8_t>().swap(input);
    }

    {
        streaming::File_Chunk_Writer temp_writer(temp_triples_path);
        compressor::utils::_buffer pending;
        auto bytes = record_io::triples_to_u8(triples, pending);
        temp_writer.write_chunk(bytes);
    }

    {
        std::vector<Triple>().swap(triples);
    }
}

void runDeflateEncodingPhase(
    const std::string& temp_triples_path,
    const std::string& output_path,
    const DeflateConfig& config,
    size_t chunk_size) {
    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }

    std::vector<Triple> triples;
    {
        streaming::File_Chunk_Reader temp_reader(temp_triples_path, chunk_size);
        std::vector<uint8_t> byte_carry;
        compressor::utils::_buffer pending;
        while (!temp_reader.is_end()) {
            if (core_new::is_streaming_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            auto chunk = temp_reader.read_chunk();
            auto [parsed, new_carry] = record_io::u8_to_triples(chunk, byte_carry, pending);
            byte_carry = std::move(new_carry);
            triples.insert(triples.end(), parsed.begin(), parsed.end());
        }
    }

    if (!config.encoding.use_flag_encoding && !config.use_3hfmtree) {
        triples = literalrun(triples, config.window.look_size);
    }

    compressor::utils::_buffer pending;
    std::vector<uint8_t> encoded;
    if (config.use_3hfmtree) {
        auto enc = inflate3hm_encode(triples, config.huffman_3hm, pending);
        encoded = std::move(enc.data);
    } else if (!config.encoding.use_flag_encoding) {
        encoded = encoding_triple_lz(triples, config.encoding, pending, true);
    } else {
        auto enc = inflate_encode(triples, pending);
        encoded = std::move(enc.data);
    }

    {
        std::vector<Triple>().swap(triples);
    }

    {
        std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("DeflateStreamingPipeline: cannot open " + output_path);
        }
        if (!encoded.empty()) {
            out.write(reinterpret_cast<const char*>(encoded.data()),
                      static_cast<std::streamsize>(encoded.size()));
        }
    }
}

DeflateStreamingPipeline::DeflateStreamingPipeline(
    DeflateConfig config, DeflateStreamingOptions options)
    : config_(std::move(config)), options_(std::move(options)) {}

void DeflateStreamingPipeline::compress_file(
    const std::string& input_path, const std::string& output_path) {
    fs::create_directories(options_.workspace_dir);

    std::string temp_triples =
        (fs::path(options_.workspace_dir) / options_.temp_triples_name).string();

    std::error_code ec;
    fs::remove(temp_triples, ec);

    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }

    runDeflateMatchingPhase(input_path, temp_triples, config_, options_.chunk_size);

    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }

    runDeflateEncodingPhase(temp_triples, output_path, config_, options_.chunk_size);

    fs::remove(temp_triples, ec);
}

}  // namespace compressor::algorithm::pipeline
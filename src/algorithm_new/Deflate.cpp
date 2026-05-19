#include "Deflate.hpp"

#include <algorithm>
#include <cstring>

namespace compressor::algorithm {

std::vector<Triple> Deflate::hashChainMatch(const std::vector<uint8_t>& input) const {
    std::vector<Triple> result;
    const size_t n = input.size();
    if (n == 0) return result;

    const size_t search_size = config_.window.search_size;
    const size_t min_match = config_.window.min_match_len;
    const size_t max_match = config_.window.look_size;
    const size_t hash_size = std::max(search_size, size_t{256});
    const size_t null_ptr = size_t{0} - 1;

    std::vector<size_t> head(hash_size, null_ptr);
    std::vector<size_t> prev(n, null_ptr);

    auto hash3 = [hash_size](uint8_t a, uint8_t b, uint8_t c) -> size_t {
        return ((static_cast<size_t>(a) << 10) ^
                (static_cast<size_t>(b) << 5) ^
                static_cast<size_t>(c)) & (hash_size - 1);
    };

    size_t cursor = 0;
    while (cursor < n) {
        size_t best_off = 0;
        size_t best_len = 0;

        if (cursor + 2 < n) {
            size_t h = hash3(input[cursor], input[cursor + 1], input[cursor + 2]);
            size_t match_pos = head[h];
            size_t chain_len = 256;

            while (match_pos != null_ptr && chain_len-- > 0) {
                size_t dist = cursor - match_pos;
                if (dist > search_size || dist == 0) break;

                size_t cur_len = 0;
                while (cur_len < max_match &&
                       cursor + cur_len < n &&
                       input[match_pos + cur_len] == input[cursor + cur_len]) {
                    ++cur_len;
                }
                if (cur_len > best_len) {
                    best_len = cur_len;
                    best_off = dist;
                    if (best_len >= max_match) break;
                }
                match_pos = prev[match_pos];
            }

            prev[cursor] = head[h];
            head[h] = cursor;
        }

        if (best_len >= min_match) {
            result.emplace_back(static_cast<uint32_t>(best_off),
                                static_cast<uint32_t>(best_len), 0);
            for (size_t i = 1; i < best_len && cursor + i + 2 < n; ++i) {
                size_t h = hash3(input[cursor + i], input[cursor + i + 1], input[cursor + i + 2]);
                prev[cursor + i] = head[h];
                head[h] = cursor + i;
            }
            cursor += best_len;
        } else {
            result.emplace_back(0, 1, input[cursor]);
            ++cursor;
        }
    }
    return result;
}

}  // namespace compressor::algorithm

// ──── Pipeline ────

#include <filesystem>
#include <fstream>
#include <utility>

#include "EncodingTriple.hpp"
#include "RecordIO.hpp"
#include "Streaming.hpp"

namespace compressor::algorithm::pipeline {

namespace fs = std::filesystem;

DeflateNonStreamingResult compress_bytes_deflate(
    const std::vector<uint8_t>& input,
    const DeflateConfig& config) {
    DeflateNonStreamingResult result;
    if (input.empty()) return result;

    Deflate deflate(config);
    auto triples = deflate.hashChainMatch(input);

    compressor::utils::_buffer pending;
    result.triples = triples;
    result.compressed = deflate.huffmanEncode(triples);
    (void)pending;
    return result;
}

std::vector<uint8_t> decompress_bytes_deflate(
    const std::vector<uint8_t>& compressed,
    const DeflateConfig& config) {
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

    Deflate deflate(config_);
    auto triples = deflate.hashChainMatch(full_input);
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

        auto encoded = deflate.huffmanEncode(all_triples);
        if (!encoded.empty()) {
            std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(encoded.data()),
                      static_cast<std::streamsize>(encoded.size()));
        }
    }
}

}  // namespace compressor::algorithm::pipeline
#include "pipeline/Phase2Dpbacktrack.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <utility>

#include "RecordIO.hpp"
#include "Streaming.hpp"

namespace compressor::algorithm::pipeline {

namespace {

void load_all_dp_reverse_records(
    const std::string& temp_a_path,
    size_t chunk_size,
    size_t total_input_bytes,
    std::vector<models::DPNode>& dp_store) {
    const size_t record_bytes = record_io::kDPNodeRecordBytes;
    const size_t expected_file_bytes = total_input_bytes * record_bytes;

    std::ifstream file(temp_a_path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Phase2: cannot open TempA");
    }

    file.seekg(0, std::ios::end);
    const size_t file_bytes = static_cast<size_t>(file.tellg());
    if (file_bytes != expected_file_bytes) {
        throw std::runtime_error("Phase2: TempA size mismatch");
    }

    const size_t records_per_chunk = std::max(size_t{1}, chunk_size / record_bytes);
    std::vector<uint8_t> raw;
    raw.resize(records_per_chunk * record_bytes);

    size_t fill_end = total_input_bytes;

    while (fill_end > 0) {
        const size_t count = std::min(records_per_chunk, fill_end);
        const size_t byte_off = (fill_end - count) * record_bytes;

        file.seekg(static_cast<std::streamoff>(byte_off));
        file.read(reinterpret_cast<char*>(raw.data()),
                  static_cast<std::streamsize>(count * record_bytes));

        std::vector<uint8_t> slice(raw.begin(),
                                   raw.begin() + static_cast<std::ptrdiff_t>(count * record_bytes));
        compressor::utils::_buffer bit_pending;
        auto nodes = record_io::parse_dp_nodes_bytes(slice, bit_pending);
        if (nodes.size() != count) {
            throw std::runtime_error("Phase2: DP record parse count mismatch");
        }

        const size_t begin = fill_end - count;
        for (size_t i = 0; i < count; ++i) {
            dp_store[begin + i] = nodes[i];
        }
        fill_end = begin;
    }
}

void write_temp_b_reverse(
    const std::vector<Triple>& triples,
    const std::string& temp_b_path) {
    const size_t nbytes = triples.size() * record_io::kTripleRecordBytes;
    streaming::Reverse_File_Chunk_Writer writer(temp_b_path);
    writer.preallocate(nbytes);

    compressor::utils::_buffer pending;
    std::vector<uint8_t> chunk;
    chunk.reserve(record_io::kTripleRecordBytes * 64);

    for (auto it = triples.rbegin(); it != triples.rend(); ++it) {
        auto rec = record_io::triple_to_record_bytes(*it, pending);
        if (rec.size() != record_io::kTripleRecordBytes) {
            throw std::runtime_error("Phase2: triple record size mismatch");
        }
        chunk.insert(chunk.end(), rec.begin(), rec.end());
    }

    if (!chunk.empty()) {
        writer.write_chunk_reverse(chunk);
    }
}

}  // namespace

Phase2Result run_phase2_dpbacktrack(
    LZDP& lzdp,
    const Phase1Result& phase1,
    const std::string& temp_a_path,
    const std::string& temp_b_path,
    size_t chunk_size) {
    Phase2Result result;

    const size_t n = phase1.total_input_bytes;
    result.total_tokens =
        phase1.terminal_node.literal_count + phase1.terminal_node.match_count;

    if (n == 0) {
        streaming::Reverse_File_Chunk_Writer writer(temp_b_path);
        writer.preallocate(0);
        return result;
    }

    std::vector<models::DPNode> dp_store(n);
    load_all_dp_reverse_records(temp_a_path, chunk_size, n, dp_store);

    int cur_pos = 0;
    result.triples = lzdp.dpbacktrack(dp_store, cur_pos, 0);

    if (result.triples.size() != result.total_tokens) {
        throw std::runtime_error("Phase2: token count mismatch");
    }

    write_temp_b_reverse(result.triples, temp_b_path);
    return result;
}

}  // namespace compressor::algorithm::pipeline

#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "ByteView.hpp"
#include "LZDP.hpp"
#include "RecordIO.hpp"
#include "Streaming.hpp"
#include "pipeline/Phase1Dpforward.hpp"
#include "pipeline/Phase2Dpbacktrack.hpp"

namespace fs = std::filesystem;

static void write_bytes(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
}

static std::vector<compressor::algorithm::Triple> load_temp_b_forward(
    const std::string& path, size_t chunk_size, size_t expected_count) {
    compressor::algorithm::streaming::File_Chunk_Reader reader(path, chunk_size);
    std::vector<uint8_t> carry;
    compressor::utils::_buffer bits;
    std::vector<compressor::algorithm::Triple> all;
    while (!reader.is_end()) {
        auto chunk = reader.read_chunk();
        auto parsed =
            compressor::algorithm::record_io::u8_to_triples(chunk, carry, bits);
        all.insert(all.end(), parsed.first.begin(), parsed.first.end());
    }
    if (!carry.empty()) {
        std::vector<uint8_t> empty;
        auto parsed =
            compressor::algorithm::record_io::u8_to_triples(empty, carry, bits);
        all.insert(all.end(), parsed.first.begin(), parsed.first.end());
    }
    assert(all.size() == expected_count);
    return all;
}

int main() {
    using compressor::algorithm::LZDP;
    using compressor::algorithm::LZDPConfig;
    using compressor::algorithm::Triple;
    using compressor::algorithm::pipeline::run_phase1_dpforward;
    using compressor::algorithm::pipeline::run_phase2_dpbacktrack;

    const std::string ws = "test_phase2_ws";
    fs::create_directories(ws);

    std::vector<uint8_t> input;
    for (int i = 0; i < 20000; ++i) {
        input.push_back(static_cast<uint8_t>((i * 17 + 3) & 0xFF));
    }
    const std::string in_path = ws + "/in.bin";
    write_bytes(in_path, input);

    LZDPConfig cfg(2048, 64, 3);
    cfg.search_size = 512;
    cfg.look_size = 64;
    LZDP lzdp(cfg);

    std::vector<compressor::algorithm::models::DPNode> dp_ref(input.size());
    dp_ref[0] = compressor::algorithm::models::DPNode(
        0, 0, -1, Triple(0, 1, input[0]));
    compressor::algorithm::VectorByteInput view{input, 0};
    lzdp.dpforward(view, dp_ref, 0, input.size());

    int cur_pos = 0;
    const std::vector<Triple> triples_ref = lzdp.dpbacktrack(dp_ref, cur_pos, 0);

    const std::string temp_a = ws + "/temp_a.bin";
    const std::string temp_b = ws + "/temp_b.bin";
    const size_t chunk = 4096;

    const auto p1 = run_phase1_dpforward(lzdp, cfg, in_path, temp_a, chunk);
    assert(p1.total_input_bytes == input.size());

    const auto p2 = run_phase2_dpbacktrack(lzdp, p1, temp_a, temp_b, chunk);
    assert(p2.total_tokens == triples_ref.size());
    assert(p2.triples.size() == triples_ref.size());

    for (size_t i = 0; i < triples_ref.size(); ++i) {
        assert(p2.triples[i].offset == triples_ref[i].offset);
        assert(p2.triples[i].length == triples_ref[i].length);
        assert(p2.triples[i].literal == triples_ref[i].literal);
    }

    const auto from_b = load_temp_b_forward(temp_b, chunk, triples_ref.size());
    for (size_t i = 0; i < triples_ref.size(); ++i) {
        assert(from_b[i].offset == triples_ref[i].offset);
        assert(from_b[i].length == triples_ref[i].length);
        assert(from_b[i].literal == triples_ref[i].literal);
    }

    const size_t expected_bytes =
        triples_ref.size() * compressor::algorithm::record_io::kTripleRecordBytes;
    assert(fs::file_size(temp_b) == expected_bytes);

    std::cout << "test_algorithm_new_phase2: OK (tokens=" << triples_ref.size()
              << " temp_b=" << expected_bytes << " bytes)\n";
    return 0;
}

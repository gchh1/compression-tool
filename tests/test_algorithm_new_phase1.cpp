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

namespace fs = std::filesystem;

static void write_bytes(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
}

static std::vector<uint8_t> read_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

static std::vector<compressor::algorithm::models::DPNode> load_temp_a_dp_nodes(
    const std::string& path, size_t chunk_size, size_t expected_count) {
    compressor::algorithm::streaming::File_Chunk_Reader reader(path, chunk_size);
    std::vector<uint8_t> carry;
    compressor::utils::_buffer bits;
    std::vector<compressor::algorithm::models::DPNode> all;
    while (!reader.is_end()) {
        auto chunk = reader.read_chunk();
        auto parsed =
            compressor::algorithm::record_io::u8_to_dp_nodes(chunk, carry, bits);
        all.insert(all.end(), parsed.first.begin(), parsed.first.end());
    }
    if (!carry.empty()) {
        auto parsed =
            compressor::algorithm::record_io::u8_to_dp_nodes(carry, carry, bits);
        all.insert(all.end(), parsed.first.begin(), parsed.first.end());
    }
    assert(all.size() == expected_count);
    return all;
}

int main() {
    using compressor::algorithm::LZDP;
    using compressor::algorithm::LZDPConfig;
    using compressor::algorithm::pipeline::run_phase1_dpforward;

    const std::string ws = "test_phase1_ws";
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
        0, 0, -1, compressor::algorithm::Triple(0, 1, input[0]));
    compressor::algorithm::VectorByteInput view{input, 0};
    lzdp.dpforward(view, dp_ref, 0, input.size());

    const std::string temp_a_ref = ws + "/temp_a_ref.bin";
    {
        compressor::utils::_buffer pending;
        compressor::algorithm::streaming::File_Chunk_Writer w(temp_a_ref);
        w.write_chunk(compressor::algorithm::record_io::dp_nodes_to_u8(dp_ref, pending));
    }

    const std::string temp_a_stream = ws + "/temp_a_stream.bin";
    const size_t chunk = 4096;
    const auto p1 = run_phase1_dpforward(lzdp, cfg, in_path, temp_a_stream, chunk);
    assert(p1.total_input_bytes == input.size());

    const auto nodes_ref = load_temp_a_dp_nodes(temp_a_ref, chunk, input.size());
    const auto nodes_stream = load_temp_a_dp_nodes(temp_a_stream, chunk, input.size());

    for (size_t i = 0; i < input.size(); ++i) {
        assert(nodes_ref[i].literal_count == nodes_stream[i].literal_count);
        assert(nodes_ref[i].match_count == nodes_stream[i].match_count);
        assert(nodes_ref[i].pre_pos == nodes_stream[i].pre_pos);
        assert(nodes_ref[i].triple.offset == nodes_stream[i].triple.offset);
        assert(nodes_ref[i].triple.length == nodes_stream[i].triple.length);
        assert(nodes_ref[i].triple.literal == nodes_stream[i].triple.literal);
    }

    std::cout << "test_algorithm_new_phase1: OK (bytes=" << input.size()
              << " chunk=" << chunk << ")\n";
    return 0;
}

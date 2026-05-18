#include "pipeline/LZDPStreamingPipeline.hpp"

#include <filesystem>
#include <fstream>
#include <utility>

#include "EncodingTriple.hpp"
#include "RecordIO.hpp"
#include "Streaming.hpp"
#include "pipeline/Phase1Dpforward.hpp"
#include "pipeline/Phase2Dpbacktrack.hpp"

namespace compressor::algorithm::pipeline {

namespace fs = std::filesystem;

LZDPStreamingPipeline::LZDPStreamingPipeline(LZDPConfig config, LZDPStreamingOptions options)
    : config_(std::move(config)), options_(std::move(options)), lzdp_(config_) {}

std::string LZDPStreamingPipeline::temp_a_path() const {
    return (fs::path(options_.workspace_dir) / options_.temp_a_name).string();
}

std::string LZDPStreamingPipeline::temp_b_path() const {
    return (fs::path(options_.workspace_dir) / options_.temp_b_name).string();
}

void LZDPStreamingPipeline::compress_file(const std::string& input_path,
                                          const std::string& output_path) {
    fs::create_directories(options_.workspace_dir);

    // 阶段①：VB&lt;u8&gt; 三分块滑动 + 边读边 dpforward → TempA（§1.20）
    const Phase1Result phase1 = run_phase1_dpforward(
        lzdp_,
        config_,
        input_path,
        temp_a_path(),
        options_.chunk_size);

    const Phase2Result phase2 = run_phase2_dpbacktrack(
        lzdp_,
        phase1,
        temp_a_path(),
        temp_b_path(),
        options_.chunk_size);
    (void)phase2;

    // Phase ③a: ChunkReader(TempB) → literalrun? → encodingTriple → output
    streaming::File_Chunk_Reader emit_reader(temp_b_path(), options_.chunk_size);
    streaming::File_Chunk_Writer emit_writer(output_path);

    compressor::utils::_buffer emit_pending;
    std::vector<uint8_t> triple_carry;

    while (!emit_reader.is_end()) {
        auto chunk = emit_reader.read_chunk();
        auto parsed = record_io::u8_to_triples(chunk, triple_carry, emit_pending);
        auto tr = std::move(parsed.first);
        if (tr.empty()) {
            continue;
        }
        if (!config_.encoding.use_flag_encoding) {
            tr = literalrun(tr, config_.window.look_size);
        }
        emit_writer.write_chunk(
            encoding_triple_lz(tr, config_.encoding, emit_pending, false));
    }

    if (!triple_carry.empty()) {
        std::vector<uint8_t> empty_chunk;
        auto parsed = record_io::u8_to_triples(empty_chunk, triple_carry, emit_pending);
        auto tr = parsed.first;
        if (!config_.encoding.use_flag_encoding) {
            tr = literalrun(tr, config_.window.look_size);
        }
        if (!tr.empty()) {
            emit_writer.write_chunk(
                encoding_triple_lz(tr, config_.encoding, emit_pending, true));
        }
    } else {
        std::vector<Triple> empty;
        auto tail = encoding_triple_lz(empty, config_.encoding, emit_pending, true);
        if (!tail.empty()) {
            emit_writer.write_chunk(tail);
        }
    }
}

}  // namespace compressor::algorithm::pipeline

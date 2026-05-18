/// DPFlate memory vs streaming: phase checkpoints + payload diff across stream chunk sizes.
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "AlgorithmFactory.hpp"
#include "DPFlate.hpp"
#include "DPFlateTrace.hpp"
#include "IAlgorithm.hpp"
#include "MemoryPool.hpp"
#include "Pipeline.hpp"
#include "StreamChunkPolicy.hpp"

namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> corpus_pattern(size_t repeats) {
    const std::string pat = "DPFLATE_TRACE_";
    std::vector<uint8_t> d;
    d.reserve(pat.size() * repeats);
    for (size_t i = 0; i < repeats; ++i) {
        d.insert(d.end(), pat.begin(), pat.end());
    }
    return d;
}

std::vector<uint8_t> corpus_repeat(size_t nbytes) {
    std::vector<uint8_t> d(nbytes);
    const uint8_t pat[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (size_t i = 0; i < nbytes; ++i) {
        d[i] = pat[i % (sizeof(pat) - 1)];
    }
    return d;
}

std::vector<size_t> parse_chunk_sweep() {
    const char* env = std::getenv("DPFLATE_TRACE_CHUNKS");
    if (!env || !env[0]) {
        return {64, 128, 256, 512, 1024};
    }
    std::vector<size_t> out;
    std::stringstream ss(env);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (tok.empty()) {
            continue;
        }
        const size_t v = static_cast<size_t>(std::strtoul(tok.c_str(), nullptr, 10));
        if (v > 0) {
            out.push_back(v);
        }
    }
    if (out.empty()) {
        return {64, 128, 256, 512, 1024};
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

compressor::core::DpflatePipelineParams default_params(bool use_3hm) {
    compressor::core::DpflatePipelineParams df{};
    df.search_size = 2048;
    df.lookahead_size = 128;
    df.min_match = 4;
    df.max_chain_length = 128;
    df.dp_sub_match_max = 6;
    df.match_engine = 1;
    df.use_flag_encoding = false;
    df.use_3hfmtree = use_3hm;
    df.huffman_offset_chunk_bits = 8;
    df.huffman_length_chunk_bits = 8;
    return df;
}

void apply_params(compressor::algorithm::DPFlate& enc,
                  const compressor::core::DpflatePipelineParams& df) {
    enc.set_match_engine(static_cast<int>(df.match_engine));
    enc.set_use_flag_encoding(df.use_flag_encoding);
    enc.set_use_3hfmtree(df.use_3hfmtree);
    enc.set_huffman_offset_chunk_bits(df.huffman_offset_chunk_bits);
    enc.set_huffman_length_chunk_bits(df.huffman_length_chunk_bits);
}

auto compress_memory_dpflate(const std::vector<uint8_t>& data,
                             const compressor::core::DpflatePipelineParams& df)
    -> std::vector<uint8_t> {
    compressor::algorithm::DPFlate enc(df.search_size, df.lookahead_size, df.min_match,
                                       df.max_chain_length, df.dp_sub_match_max);
    apply_params(enc, df);

    compressor::algorithm::DPFlateTrace::instance().begin_capture("memory");
    compressor::algorithm::DPFlateTrace::instance().set_file_tag("memory");

    std::vector<uint8_t> out(data.size() * 4 + 65536);
    size_t in_off = 0;
    size_t out_pos = 0;
    for (;;) {
        if (out_pos >= out.size()) {
            out.resize(out.size() * 2 + data.size());
        }
        const auto in_span =
            std::span<const uint8_t>(data.data() + in_off, data.size() - in_off);
        const auto out_span = std::span<uint8_t>(out.data() + out_pos, out.size() - out_pos);
        const bool is_last = (in_off + in_span.size() >= data.size());
        auto st = enc.process(in_span, out_span, is_last);
        in_off += st.bytes_consumed;
        out_pos += st.bytes_produced;
        if (st.done) {
            break;
        }
        if (st.need_output && st.bytes_produced == 0) {
            out.resize(out.size() * 2 + 65536);
        }
    }
    out.resize(out_pos);
    compressor::algorithm::DPFlateTrace::instance().end_capture();
    return out;
}

/// Push exact ``stream_chunk`` bytes per pipeline call (bypasses ``effective_stream_chunk_bytes``
/// 64 KiB minimum used by ``compressFile``).
auto compress_stream_pipeline_exact(const std::vector<uint8_t>& data,
                                    const compressor::core::DpflatePipelineParams& df,
                                    size_t stream_chunk) -> std::vector<uint8_t> {
    compressor::algorithm::DPFlateTrace::instance().begin_capture("stream");
    compressor::algorithm::DPFlateTrace::instance().set_file_tag("stream");

    const size_t min_m = df.min_match == 0 ? size_t{4} : df.min_match;
    auto enc = std::make_unique<compressor::algorithm::DPFlate>(
        df.search_size, df.lookahead_size, min_m, df.max_chain_length, df.dp_sub_match_max);
    apply_params(*enc, df);

    const size_t out_pool =
        compressor::processor::pipeline_output_pool_chunk_bytes(stream_chunk);
    auto pool = std::make_shared<compressor::memory::MemoryPool>(8, out_pool);
    std::vector<std::unique_ptr<compressor::algorithm::IAlgorithm>> algos;
    algos.push_back(std::move(enc));
    compressor::processor::Pipeline pipeline(std::move(algos), pool);

    std::vector<uint8_t> out;
    out.reserve(data.size() / 2 + 65536);

    auto drain_out = [&]() {
        for (;;) {
            auto chunk = pipeline.pull();
            if (chunk.empty()) {
                break;
            }
            const auto v = chunk.view();
            out.insert(out.end(), v.begin(), v.end());
        }
    };

    for (size_t off = 0; off < data.size();) {
        const size_t n = (std::min)(stream_chunk, data.size() - off);
        const bool last = (off + n >= data.size());
        pipeline.push(std::span<const uint8_t>(data.data() + off, n), last);
        off += n;
        drain_out();
    }
    pipeline.finish();
    drain_out();

    compressor::algorithm::DPFlateTrace::instance().end_capture();
    return out;
}

void print_checkpoints(const char* run_id) {
    auto& tr = compressor::algorithm::DPFlateTrace::instance();
    const auto& cps = tr.checkpoints(run_id);
    const size_t pauses = tr.count_phase(run_id, "COLLECT_PAUSE");
    std::cerr << "--- checkpoints [" << run_id << "] count=" << cps.size()
              << " COLLECT_PAUSE=" << pauses << " ---\n";
    for (const auto& cp : cps) {
        if (cp.phase == "COLLECT_PAUSE") {
            continue;
        }
        std::cerr << "  " << cp.phase << " in_len=" << cp.total_in_len
                  << " tokens=" << cp.total_tokens << " tok_digest=0x" << std::hex
                  << cp.forward_token_digest << " freq_digest=0x" << cp.freq_digest
                  << std::dec << " writer=" << cp.writer_bytes << "\n";
    }
}

bool run_case(const std::vector<uint8_t>& data, bool use_3hm, size_t stream_chunk,
              const char* label) {
    const auto df = default_params(use_3hm);
    compressor::algorithm::DPFlateTrace::instance().clear_all();

    std::cerr << "\n[" << label << "] corpus=" << data.size() << " chunk=" << stream_chunk
              << " tree=" << (use_3hm ? "3HfM" : "FLATE") << "\n";

    const auto mem_out = compress_memory_dpflate(data, df);
    const auto stream_out = compress_stream_pipeline_exact(data, df, stream_chunk);

    print_checkpoints("memory");
    print_checkpoints("stream");

    auto& tr = compressor::algorithm::DPFlateTrace::instance();
    const size_t stream_pauses = tr.count_phase("stream", "COLLECT_PAUSE");
    const size_t expected_pauses =
        (data.size() > 0 && stream_chunk < data.size())
            ? ((data.size() + stream_chunk - 1) / stream_chunk)
            : 1;
    if (stream_chunk < data.size() && stream_pauses + 1 < expected_pauses) {
        std::cerr << "[trace] WARN: COLLECT_PAUSE=" << stream_pauses
                  << " expected about " << (expected_pauses - 1)
                  << " (small chunk boundary pressure may be weak)\n";
    }

    const auto phase_diff = tr.diff_runs("memory", "stream");
    if (!phase_diff.empty()) {
        std::cerr << "[trace] phase diff: " << phase_diff << "\n";
        return false;
    }
    std::cerr << "[trace] all final-phase checkpoints match\n";

    if (mem_out.empty() || stream_out.empty()) {
        std::cerr << "[trace] empty output (compress failed)\n";
        return false;
    }
    if (mem_out == stream_out) {
        std::cerr << "[trace] payload byte-identical (" << mem_out.size() << " bytes)\n";
        return true;
    }

    std::cerr << "[trace] payload MISMATCH mem=" << mem_out.size()
              << " stream=" << stream_out.size() << "\n";
    const size_t n = (std::min)(mem_out.size(), stream_out.size());
    for (size_t i = 0; i < n; ++i) {
        if (mem_out[i] != stream_out[i]) {
            std::cerr << "  first payload diff @" << i << " mem=0x" << std::hex
                      << static_cast<unsigned>(mem_out[i]) << " stream=0x"
                      << static_cast<unsigned>(stream_out[i]) << std::dec << "\n";
            break;
        }
    }
    return false;
}

bool run_chunk_sweep(const std::vector<uint8_t>& data, bool use_3hm,
                     const std::vector<size_t>& chunks, const char* suite) {
    bool ok = true;
    for (size_t ch : chunks) {
        std::string label = std::string(suite) + "/chunk=" + std::to_string(ch);
        if (!run_case(data, use_3hm, ch, label.c_str())) {
            ok = false;
        }
    }
    return ok;
}

}  // namespace

int main() {
    const char* trace_path = std::getenv("WEBCOMPRESS_DPFLATE_TRACE");
    if (trace_path && trace_path[0]) {
        std::cerr << "[trace] step log: " << trace_path << "\n";
    }

    const auto chunks = parse_chunk_sweep();
    std::cerr << "[trace] chunk sweep (exact pipeline push, not compressFile 64KiB min):";
    for (size_t c : chunks) {
        std::cerr << " " << c;
    }
    std::cerr << "\n  override: DPFLATE_TRACE_CHUNKS=64,128,...\n";

    bool ok = true;

    const auto data_default = corpus_pattern(512);
    std::cerr << "\n======== FLATE (pattern " << data_default.size() << " B) ========\n";
    if (!run_chunk_sweep(data_default, false, chunks, "FLATE")) {
        ok = false;
    }

    const bool include_3hm = []() {
        const char* e = std::getenv("DPFLATE_TRACE_INCLUDE_3HM");
        return e && e[0] == '1';
    }();

    if (include_3hm) {
        std::cerr << "\n======== 3HfMT pattern " << data_default.size() << " B ========\n";
        if (!run_chunk_sweep(data_default, true, chunks, "3HfM/pattern")) {
            ok = false;
        }

        const auto data_64k = corpus_repeat(64u * 1024u);
        std::cerr << "\n======== 3HfMT repeat 64KiB ========\n";
        const std::vector<size_t> stress_chunks = {64, 128, 256, 512, 1024};
        if (!run_chunk_sweep(data_64k, true, stress_chunks, "3HfM/64k")) {
            ok = false;
        }

        if (const char* big = std::getenv("DPFLATE_TRACE_STRESS_1MB");
            big && big[0] == '1') {
            const auto data_1mb = corpus_repeat(1024u * 1024u);
            std::cerr << "\n======== 3HfMT repeat 1MiB (may AV = BUG-09) ========\n";
            const std::vector<size_t> tiny_chunks = {64, 128, 256};
            if (!run_chunk_sweep(data_1mb, true, tiny_chunks, "3HfM/1mb")) {
                ok = false;
            }
        } else {
            std::cerr << "\n[trace] 1MiB 3HfM stress skipped (set DPFLATE_TRACE_STRESS_1MB=1)\n";
        }
    } else {
        std::cerr << "\n[trace] 3HfMT skipped — set DPFLATE_TRACE_INCLUDE_3HM=1\n";
    }

    std::cout << (ok ? "test_dpflate_stream_trace: PASS\n"
                     : "test_dpflate_stream_trace: FAIL\n");
    return ok ? 0 : 1;
}

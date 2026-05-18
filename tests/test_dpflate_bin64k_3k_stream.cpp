/// binary_64k (65536 B) + exact 3 KiB pipeline pushes (bypasses compressFile 64 KiB min).
/// Container dumps: WEBCOMPRESS_DPFLATE_BIN64K_DEBUG=<log路径>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <span>
#include <vector>

#include "AlgorithmFactory.hpp"
#include "DPFlate.hpp"
#include "DPFlateBin64kDebug.hpp"
#include "IAlgorithm.hpp"
#include "MemoryPool.hpp"
#include "Pipeline.hpp"
#include "StreamChunkPolicy.hpp"

namespace {

constexpr size_t kFileBytes = 64u * 1024u;
constexpr size_t kStreamChunkBytes = 3u * 1024u;

std::vector<uint8_t> corpus_binary_64k() {
    std::vector<uint8_t> d(kFileBytes);
    std::mt19937 gen(67890);
    for (auto& b : d) {
        b = static_cast<uint8_t>(gen() & 0xFF);
    }
    return d;
}

compressor::core::DpflatePipelineParams default_dp_params(bool use_flag) {
    compressor::core::DpflatePipelineParams df{};
    df.search_size = 4096;
    df.lookahead_size = 256;
    df.min_match = 4;
    df.max_chain_length = 256;
    df.dp_sub_match_max = 6;
    df.match_engine = 1;
    df.use_flag_encoding = use_flag;
    df.use_3hfmtree = true;
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

std::vector<uint8_t> compress_memory(const std::vector<uint8_t>& data,
                                     const compressor::core::DpflatePipelineParams& df) {
    compressor::algorithm::DPFlate enc(df.search_size, df.lookahead_size, df.min_match,
                                       df.max_chain_length, df.dp_sub_match_max);
    apply_params(enc, df);

    std::vector<uint8_t> out(data.size() * 2 + 131072);
    size_t in_off = 0;
    size_t out_pos = 0;
    for (;;) {
        if (out_pos >= out.size()) {
            out.resize(out.size() * 2 + 65536);
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
    return out;
}

std::vector<uint8_t> compress_stream_3k(const std::vector<uint8_t>& data,
                                        const compressor::core::DpflatePipelineParams& df) {
    auto enc = std::make_unique<compressor::algorithm::DPFlate>(
        df.search_size, df.lookahead_size, df.min_match, df.max_chain_length, df.dp_sub_match_max);
    apply_params(*enc, df);

    const size_t out_pool =
        compressor::processor::pipeline_output_pool_chunk_bytes(kStreamChunkBytes);
    auto pool = std::make_shared<compressor::memory::MemoryPool>(8, out_pool);
    std::vector<std::unique_ptr<compressor::algorithm::IAlgorithm>> algos;
    algos.push_back(std::move(enc));
    compressor::processor::Pipeline pipeline(std::move(algos), pool);

    std::vector<uint8_t> out;
    out.reserve(data.size() / 2 + 131072);

    auto drain = [&]() {
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
        const size_t n = (std::min)(kStreamChunkBytes, data.size() - off);
        const bool last = (off + n >= data.size());
        pipeline.push(std::span<const uint8_t>(data.data() + off, n), last);
        off += n;
        drain();
    }
    pipeline.finish();
    drain();
    return out;
}

bool run_flag(bool use_flag, const std::vector<uint8_t>& data) {
    const auto df = default_dp_params(use_flag);
    const auto mem = compress_memory(data, df);
    const auto stream = compress_stream_3k(data, df);
    if (mem.empty() || stream.empty()) {
        fprintf(stderr, "[bin64k_3k] flag=%d empty output mem=%zu stream=%zu\n", use_flag ? 1 : 0,
                mem.size(), stream.size());
        return false;
    }
    if (mem != stream) {
        fprintf(stderr, "[bin64k_3k] flag=%d payload mismatch mem=%zu stream=%zu\n",
                use_flag ? 1 : 0, mem.size(), stream.size());
        const size_t n = (std::min)(mem.size(), stream.size());
        for (size_t i = 0; i < n; ++i) {
            if (mem[i] != stream[i]) {
                fprintf(stderr, "[bin64k_3k] first diff @%zu mem=0x%02x stream=0x%02x\n", i,
                        mem[i], stream[i]);
                break;
            }
        }
        return false;
    }
    fprintf(stderr, "[bin64k_3k] flag=%d PASS mem==stream payload=%zu bytes\n", use_flag ? 1 : 0,
            mem.size());
    return true;
}

}  // namespace

int main() {
    const auto data = corpus_binary_64k();
    fprintf(stderr, "[bin64k_3k] file=%zu B pipeline_chunk=%zu B (~%zu pushes)\n", data.size(),
            kStreamChunkBytes, (data.size() + kStreamChunkBytes - 1) / kStreamChunkBytes);

    if (const char* dbg = std::getenv("WEBCOMPRESS_DPFLATE_BIN64K_DEBUG")) {
        compressor::algorithm::DPFlateBin64kDebug::init_once();
        compressor::algorithm::DPFlateBin64kDebug::arm_session(static_cast<uint32_t>(kFileBytes),
                                                               true);
        fprintf(stderr, "[bin64k_3k] container debug -> %s\n",
                (dbg[0] == '1' && dbg[1] == '\0') ? "dpflate_bin64k_containers.log" : dbg);
    } else {
        fprintf(stderr, "[bin64k_3k] hint: WEBCOMPRESS_DPFLATE_BIN64K_DEBUG=<log> for dumps\n");
    }

    const bool ok = run_flag(false, data) && run_flag(true, data);
    fprintf(stderr, "[bin64k_3k] %s\n", ok ? "ALL PASS" : "FAIL");
    return ok ? 0 : 1;
}

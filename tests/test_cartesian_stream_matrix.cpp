/// P7/P8 Cartesian sub-matrix: stream chunks 30/57/97/113 KiB × corpora 256/522/1025/2000 KiB.
/// Condition A: exact Pipeline push payload ≡ DPFlateCompressor memory.
/// Condition B: compressFile → decompressFile roundtrip (needs stream-chunk floor ≤ 30 KiB).
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "AlgorithmFactory.hpp"
#include "DPFlate.hpp"
#include "DPFlateCompressor.hpp"
#include "IAlgorithm.hpp"
#include "MemoryPool.hpp"
#include "Pipeline.hpp"
#include "StreamChunkPolicy.hpp"
#include "api.hpp"

namespace fs = std::filesystem;

namespace {

constexpr size_t kChunks[] = {
    30u * 1024u,
    57u * 1024u,
    97u * 1024u,
    113u * 1024u,
};
constexpr size_t kSizes[] = {
    256u * 1024u,
    522u * 1024u,
    1025u * 1024u,
    2000u * 1024u,
};

bool env_truthy(const char* name) {
    const char* v = std::getenv(name);
    return v && (v[0] == '1' || v[0] == 'y' || v[0] == 'Y');
}

void write_all(const fs::path& p, const std::vector<uint8_t>& d) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
}

std::vector<uint8_t> read_all(const fs::path& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) {
        return {};
    }
    const auto sz = f.tellg();
    if (sz <= 0) {
        return {};
    }
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return b;
}

std::vector<uint8_t> corpus_repeat(size_t nbytes) {
    std::vector<uint8_t> d(nbytes);
    const uint8_t pat[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (size_t i = 0; i < nbytes; ++i) {
        d[i] = pat[i % (sizeof(pat) - 1)];
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

std::vector<uint8_t> compress_memory_compressor(const std::vector<uint8_t>& data,
                                                const compressor::core::DpflatePipelineParams& df) {
    compressor::core::DPFlateCompressor mem;
    mem.set_search_size(df.search_size);
    mem.set_lookahead_size(df.lookahead_size);
    mem.set_min_match(df.min_match);
    mem.set_max_chain_length(df.max_chain_length);
    mem.set_dp_sub_match_max(df.dp_sub_match_max);
    mem.set_match_engine(static_cast<int>(df.match_engine));
    mem.set_use_flag_encoding(df.use_flag_encoding);
    mem.set_use_3hfmtree(df.use_3hfmtree);
    mem.set_huffman_offset_chunk_bits(df.huffman_offset_chunk_bits);
    mem.set_huffman_length_chunk_bits(df.huffman_length_chunk_bits);
    const auto cr = mem.compress(data);
    if (!cr.success) {
        return {};
    }
    return cr.data;
}

std::vector<uint8_t> compress_stream_exact(const std::vector<uint8_t>& data,
                                           const compressor::core::DpflatePipelineParams& df,
                                           size_t stream_chunk) {
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
        const size_t n = (std::min)(stream_chunk, data.size() - off);
        const bool last = (off + n >= data.size());
        pipeline.push(std::span<const uint8_t>(data.data() + off, n), last);
        off += n;
        drain();
    }
    pipeline.finish();
    drain();
    return out;
}

bool condition_a(size_t file_bytes, size_t chunk_bytes, bool use_flag) {
    const auto df = default_dp_params(use_flag);
    const auto data = corpus_repeat(file_bytes);
    const auto mem = compress_memory_compressor(data, df);
    const auto stream = compress_stream_exact(data, df, chunk_bytes);
    if (mem.empty()) {
        fprintf(stderr, "[cartesian] A FAIL mem compress size=%zu chunk=%zu flag=%d\n",
                file_bytes, chunk_bytes, use_flag ? 1 : 0);
        return false;
    }
    if (mem != stream) {
        fprintf(stderr,
                "[cartesian] A FAIL payload size=%zu chunk=%zu flag=%d mem=%zu stream=%zu\n",
                file_bytes, chunk_bytes, use_flag ? 1 : 0, mem.size(), stream.size());
        const size_t n = (std::min)(mem.size(), stream.size());
        for (size_t i = 0; i < n; ++i) {
            if (mem[i] != stream[i]) {
                fprintf(stderr, "  first diff @%zu mem=0x%02x stream=0x%02x\n", i, mem[i],
                        stream[i]);
                break;
            }
        }
        return false;
    }
    fprintf(stderr, "[cartesian] A PASS size=%zu chunk=%zu flag=%d payload=%zu\n", file_bytes,
            chunk_bytes, use_flag ? 1 : 0, mem.size());
    return true;
}

bool condition_b(const fs::path& work, size_t file_bytes, size_t chunk_bytes, bool use_flag) {
    using compressor::api::AlgorithmID;
    using compressor::api::compressFile;
    using compressor::api::decompressFile;

    const auto df = default_dp_params(use_flag);
    const auto data = corpus_repeat(file_bytes);
    const fs::path in = work / ("in_" + std::to_string(file_bytes) + ".bin");
    const fs::path wcx = work / ("out_" + std::to_string(file_bytes) + "_" +
                                 std::to_string(chunk_bytes) + ".wcx");
    const fs::path dec = work / ("dec_" + std::to_string(file_bytes) + ".bin");
    write_all(in, data);

    const AlgorithmID ch[] = {AlgorithmID::DPFlate};
    const size_t eff_chunk = compressor::processor::effective_stream_chunk_bytes(chunk_bytes);
    auto cr = compressFile(in.string(), wcx.string(), ch, eff_chunk,
                           compressor::core::kFileCompressOptsNone, nullptr, &df);
    if (!cr.success) {
        fprintf(stderr,
                "[cartesian] B FAIL compress size=%zu req_chunk=%zu eff=%zu flag=%d: %s\n",
                file_bytes, chunk_bytes, eff_chunk, use_flag ? 1 : 0, cr.error_message.c_str());
        return false;
    }
    const AlgorithmID de[] = {AlgorithmID::Inflate};
    auto dr = decompressFile(wcx.string(), dec.string(), de, eff_chunk);
    if (!dr.success) {
        fprintf(stderr, "[cartesian] B FAIL decompress size=%zu chunk=%zu flag=%d: %s\n",
                file_bytes, eff_chunk, use_flag ? 1 : 0, dr.error_message.c_str());
        return false;
    }
    const auto got = read_all(dec);
    if (got != data) {
        fprintf(stderr, "[cartesian] B FAIL roundtrip size=%zu chunk=%zu flag=%d dec=%zu\n",
                file_bytes, eff_chunk, use_flag ? 1 : 0, got.size());
        return false;
    }
    fprintf(stderr, "[cartesian] B PASS size=%zu chunk=%zu eff=%zu flag=%d comp=%zu\n", file_bytes,
            chunk_bytes, eff_chunk, use_flag ? 1 : 0, cr.compressed_size);
    return true;
}

}  // namespace

int main() {
#ifdef _WIN32
    _putenv_s("WEBCOMPRESS_STREAM_CHUNK_MIN_BYTES", "30720");
#else
    setenv("WEBCOMPRESS_STREAM_CHUNK_MIN_BYTES", "30720", 1);
#endif

    const bool skip_b = env_truthy("CARTESIAN_SKIP_B");
    const bool quick = env_truthy("CARTESIAN_QUICK");

    fs::path work = fs::temp_directory_path() / "cartesian_stream_matrix_test";
    if (const char* w = std::getenv("WEBCOMPRESS_TEST_WORKDIR"); w && w[0]) {
        work = fs::path(w) / "cartesian_stream_matrix_test";
    }
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);

    fprintf(stderr,
            "[cartesian] P7/P8 sub-matrix chunks=30/57/97/113 KiB sizes=256/522/1025/2000 KiB "
            "skip_B=%d quick=%d floor=%zu\n",
            skip_b ? 1 : 0, quick ? 1 : 0,
            compressor::processor::stream_chunk_floor_bytes());

    bool ok = true;
    const size_t n_chunk = quick ? 1u : (sizeof(kChunks) / sizeof(kChunks[0]));
    const size_t n_size = quick ? 1u : (sizeof(kSizes) / sizeof(kSizes[0]));

    for (size_t ic = 0; ic < n_chunk; ++ic) {
        const size_t chunk = kChunks[ic];
        for (size_t is = 0; is < n_size; ++is) {
            const size_t size = kSizes[is];
            for (const bool use_flag : {false, true}) {
                if (!condition_a(size, chunk, use_flag)) {
                    ok = false;
                }
                if (!skip_b && !condition_b(work, size, chunk, use_flag)) {
                    ok = false;
                }
            }
        }
    }

    fs::remove_all(work, ec);
    fprintf(stderr, "[cartesian] %s\n", ok ? "ALL PASS" : "FAIL");
    return ok ? 0 : 1;
}

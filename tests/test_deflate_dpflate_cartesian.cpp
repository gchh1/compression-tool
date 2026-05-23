#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Deflate.hpp"
#include "Dpflate.hpp"
#include "WCXProtocol.hpp"
#include "old_dpflate_bridge.h"

namespace fs = std::filesystem;

namespace {

constexpr size_t kCorpusSizes[] = {
    64u * 1024u,
    256u * 1024u,
    512u * 1024u,
    2048u * 1024u,
};
constexpr size_t kChunkSize = 300u * 1024u;

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
    if (!f) return {};
    const auto sz = f.tellg();
    if (sz <= 0) return {};
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return b;
}

std::vector<uint8_t> make_corpus(size_t nbytes) {
    std::vector<uint8_t> d(nbytes);
    const uint8_t pat[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (size_t i = 0; i < nbytes; ++i) {
        d[i] = pat[i % (sizeof(pat) - 1)];
    }
    return d;
}

template<typename F>
bool check_wcx_roundtrip(const std::vector<uint8_t>& compressed,
                         const std::vector<uint8_t>& original,
                         F&& decompress_fn,
                         uint8_t algo_code) {
    using compressor::api_new::wcx::buildHeaderBytes;
    using compressor::api_new::wcx::tryParseHeader;
    using compressor::api_new::wcx::HeaderView;

    auto header = buildHeaderBytes(algo_code,
                                   static_cast<uint32_t>(original.size()),
                                   static_cast<uint32_t>(compressed.size()),
                                   "testfile.bin");
    std::vector<uint8_t> wcx_blob = header;
    wcx_blob.insert(wcx_blob.end(), compressed.begin(), compressed.end());

    HeaderView hv;
    if (!tryParseHeader({wcx_blob.data(), wcx_blob.size()}, hv)) {
        fprintf(stderr, "    [WCX] FAIL parse header\n");
        return false;
    }
    if (hv.original_size != original.size()) {
        fprintf(stderr, "    [WCX] FAIL original_size mismatch: %u != %zu\n",
                hv.original_size, original.size());
        return false;
    }
    if (hv.compressed_size != compressed.size()) {
        fprintf(stderr, "    [WCX] FAIL compressed_size mismatch: %u != %zu\n",
                hv.compressed_size, compressed.size());
        return false;
    }

    size_t payload_offset = hv.total_size;
    std::vector<uint8_t> payload(wcx_blob.begin() + payload_offset, wcx_blob.end());
    if (payload.size() != compressed.size()) {
        fprintf(stderr, "    [WCX] FAIL payload size mismatch: %zu != %zu\n",
                payload.size(), compressed.size());
        return false;
    }

    auto dec_wcx = decompress_fn(payload);
    if (dec_wcx != original) {
        fprintf(stderr, "    [WCX] FAIL decompress mismatch: dec=%zu orig=%zu\n",
                dec_wcx.size(), original.size());
        return false;
    }

    return true;
}

// ──── Deflate FLATE ────

bool test_deflate_flate(size_t corpus_size, bool use_flag) {
    using compressor::algorithm::DeflateConfig;
    using compressor::algorithm::compress_bytes_deflate;
    using compressor::algorithm::decompress_bytes_deflate;
    using compressor::algorithm::pipeline::DeflateStreamingPipeline;
    using compressor::algorithm::pipeline::DeflateStreamingOptions;

    const auto data = make_corpus(corpus_size);

    DeflateConfig cfg(32768, 258, 256, use_flag, false);

    auto mem_result = compress_bytes_deflate(data, cfg);
    if (mem_result.compressed.empty()) {
        fprintf(stderr, "  [Deflate/FLATE] FAIL mem compress size=%zu flag=%d\n",
                corpus_size, use_flag ? 1 : 0);
        return false;
    }

    auto dec_mem = decompress_bytes_deflate(mem_result.compressed, cfg);
    if (dec_mem != data) {
        fprintf(stderr, "  [Deflate/FLATE] FAIL B-mem size=%zu flag=%d dec=%zu\n",
                corpus_size, use_flag ? 1 : 0, dec_mem.size());
        return false;
    }

    {
        auto dec_wcx_fn = [&](const std::vector<uint8_t>& c) {
            return decompress_bytes_deflate(c, cfg);
        };
        if (!check_wcx_roundtrip(mem_result.compressed, data, dec_wcx_fn, 1)) {
            fprintf(stderr, "  [Deflate/FLATE] FAIL WCX size=%zu flag=%d\n",
                    corpus_size, use_flag ? 1 : 0);
            return false;
        }
    }

    const fs::path work = fs::temp_directory_path() / "deflate_cartesian";
    std::error_code ec;
    fs::create_directories(work, ec);

    const fs::path in_path = work / "in.bin";
    const fs::path out_path = work / "out.wcx";
    write_all(in_path, data);

    DeflateStreamingOptions opts;
    opts.chunk_size = kChunkSize;
    opts.workspace_dir = work.string();

    DeflateStreamingPipeline pipeline(cfg, opts);
    pipeline.compress_file(in_path.string(), out_path.string());

    auto stream_compressed = read_all(out_path);
    if (stream_compressed.empty()) {
        fprintf(stderr, "  [Deflate/FLATE] FAIL stream compress size=%zu flag=%d\n",
                corpus_size, use_flag ? 1 : 0);
        fs::remove_all(work, ec);
        return false;
    }

    auto dec_stream = decompress_bytes_deflate(stream_compressed, cfg);
    if (dec_stream != data) {
        fprintf(stderr, "  [Deflate/FLATE] FAIL B-stream size=%zu flag=%d dec=%zu\n",
                corpus_size, use_flag ? 1 : 0, dec_stream.size());
        fs::remove_all(work, ec);
        return false;
    }

    if (mem_result.compressed != stream_compressed) {
        fprintf(stderr,
                "  [Deflate/FLATE] FAIL A mem!=stream size=%zu flag=%d mem=%zu stream=%zu\n",
                corpus_size, use_flag ? 1 : 0,
                mem_result.compressed.size(), stream_compressed.size());
        fs::remove_all(work, ec);
        return false;
    }

    fs::remove_all(work, ec);

    fprintf(stderr,
            "  [Deflate/FLATE] PASS size=%zu flag=%d payload=%zu (B)\n",
            corpus_size, use_flag ? 1 : 0, mem_result.compressed.size());
    return true;
}

// ──── Deflate 3HfMT ────

bool test_deflate_3hm(size_t corpus_size, bool use_flag) {
    using compressor::algorithm::DeflateConfig;
    using compressor::algorithm::compress_bytes_deflate;
    using compressor::algorithm::decompress_bytes_deflate;
    using compressor::algorithm::pipeline::DeflateStreamingPipeline;
    using compressor::algorithm::pipeline::DeflateStreamingOptions;

    const auto data = make_corpus(corpus_size);

    DeflateConfig cfg(32768, 258, 256, use_flag, true);

    auto mem_result = compress_bytes_deflate(data, cfg);
    if (mem_result.compressed.empty()) {
        fprintf(stderr, "  [Deflate/3HfMT] FAIL mem compress size=%zu flag=%d\n",
                corpus_size, use_flag ? 1 : 0);
        return false;
    }

    auto dec_mem = decompress_bytes_deflate(mem_result.compressed, cfg);
    if (dec_mem != data) {
        fprintf(stderr, "  [Deflate/3HfMT] FAIL B-mem size=%zu flag=%d dec=%zu\n",
                corpus_size, use_flag ? 1 : 0, dec_mem.size());
        return false;
    }

    {
        auto dec_wcx_fn = [&](const std::vector<uint8_t>& c) {
            return decompress_bytes_deflate(c, cfg);
        };
        if (!check_wcx_roundtrip(mem_result.compressed, data, dec_wcx_fn, 1)) {
            fprintf(stderr, "  [Deflate/3HfMT] FAIL WCX size=%zu flag=%d\n",
                    corpus_size, use_flag ? 1 : 0);
            return false;
        }
    }

    const fs::path work = fs::temp_directory_path() / "deflate_3hm_cartesian";
    std::error_code ec;
    fs::create_directories(work, ec);

    const fs::path in_path = work / "in.bin";
    const fs::path out_path = work / "out.wcx";
    write_all(in_path, data);

    DeflateStreamingOptions opts;
    opts.chunk_size = kChunkSize;
    opts.workspace_dir = work.string();

    DeflateStreamingPipeline pipeline(cfg, opts);
    pipeline.compress_file(in_path.string(), out_path.string());

    auto stream_compressed = read_all(out_path);
    if (stream_compressed.empty()) {
        fprintf(stderr, "  [Deflate/3HfMT] FAIL stream compress size=%zu flag=%d\n",
                corpus_size, use_flag ? 1 : 0);
        fs::remove_all(work, ec);
        return false;
    }

    auto dec_stream = decompress_bytes_deflate(stream_compressed, cfg);
    if (dec_stream != data) {
        fprintf(stderr, "  [Deflate/3HfMT] FAIL B-stream size=%zu flag=%d dec=%zu\n",
                corpus_size, use_flag ? 1 : 0, dec_stream.size());
        fs::remove_all(work, ec);
        return false;
    }

    if (mem_result.compressed != stream_compressed) {
        fprintf(stderr,
                "  [Deflate/3HfMT] FAIL A mem!=stream size=%zu flag=%d mem=%zu stream=%zu\n",
                corpus_size, use_flag ? 1 : 0,
                mem_result.compressed.size(), stream_compressed.size());
        fs::remove_all(work, ec);
        return false;
    }

    fs::remove_all(work, ec);

    fprintf(stderr,
            "  [Deflate/3HfMT] PASS size=%zu flag=%d payload=%zu (B)\n",
            corpus_size, use_flag ? 1 : 0, mem_result.compressed.size());
    return true;
}

// ──── DPFlate FLATE ────

bool test_dpflate_flate(size_t corpus_size, bool use_flag) {
    using compressor::algorithm::DPFlateConfig;
    using compressor::algorithm::compress_bytes_dpflate;
    using compressor::algorithm::decompress_bytes_dpflate;
    using compressor::algorithm::pipeline::DPFlateStreamingPipeline;
    using compressor::algorithm::pipeline::DPFlateStreamingOptions;

    const auto data = make_corpus(corpus_size);

    DPFlateConfig cfg(4095, 255, 3, use_flag, false, 0, 4);

    auto mem_result = compress_bytes_dpflate(data, cfg);
    fprintf(stderr, "  [DPFlate/FLATE] NEW compress done: compressed=%zu\n", mem_result.compressed.size());
    fflush(stderr);
    if (mem_result.compressed.empty()) {
        fprintf(stderr, "  [DPFlate/FLATE] FAIL mem compress size=%zu flag=%d\n",
                corpus_size, use_flag ? 1 : 0);
        return false;
    }

    auto dec_mem = decompress_bytes_dpflate(mem_result.compressed, cfg);
    if (dec_mem != data) {
        fprintf(stderr, "  [DPFlate/FLATE] FAIL B-mem size=%zu flag=%d dec=%zu\n",
                corpus_size, use_flag ? 1 : 0, dec_mem.size());
        return false;
    }

    {
        auto dec_wcx_fn = [&](const std::vector<uint8_t>& c) {
            return decompress_bytes_dpflate(c, cfg);
        };
        if (!check_wcx_roundtrip(mem_result.compressed, data, dec_wcx_fn, 5)) {
            fprintf(stderr, "  [DPFlate/FLATE] FAIL WCX size=%zu flag=%d\n",
                    corpus_size, use_flag ? 1 : 0);
            return false;
        }
    }

    const fs::path work = fs::temp_directory_path() / "dpflate_flate_cartesian";
    std::error_code ec;
    fs::create_directories(work, ec);

    const fs::path in_path = work / "in.bin";
    const fs::path out_path = work / "out.wcx";
    write_all(in_path, data);

    DPFlateStreamingOptions opts;
    opts.chunk_size = kChunkSize;
    opts.workspace_dir = work.string();

    DPFlateStreamingPipeline pipeline(cfg, opts);
    fprintf(stderr, "  [DPFlate/FLATE] streaming compress start...\n");
    fflush(stderr);
    pipeline.compress_file(in_path.string(), out_path.string());
    fprintf(stderr, "  [DPFlate/FLATE] streaming compress done.\n");
    fflush(stderr);

    auto stream_compressed = read_all(out_path);
    if (stream_compressed.empty()) {
        fprintf(stderr, "  [DPFlate/FLATE] FAIL stream compress size=%zu flag=%d\n",
                corpus_size, use_flag ? 1 : 0);
        fs::remove_all(work, ec);
        return false;
    }

    auto dec_stream = decompress_bytes_dpflate(stream_compressed, cfg);
    if (dec_stream != data) {
        fprintf(stderr, "  [DPFlate/FLATE] FAIL B-stream size=%zu flag=%d dec=%zu\n",
                corpus_size, use_flag ? 1 : 0, dec_stream.size());
        fs::remove_all(work, ec);
        return false;
    }

    if (mem_result.compressed != stream_compressed) {
        fprintf(stderr,
                "  [DPFlate/FLATE] FAIL A mem!=stream size=%zu flag=%d mem=%zu stream=%zu\n",
                corpus_size, use_flag ? 1 : 0,
                mem_result.compressed.size(), stream_compressed.size());
        fs::remove_all(work, ec);
        return false;
    }

    // === OLD vs NEW comparison ===
    {
        fprintf(stderr, "  [DPFlate/FLATE] OLD compress starting...\n");
        fflush(stderr);
        auto old_compressed = test_bridge::old_dpflate_compress(
            data, 4095, 255, 4, 3, 6, false, 8);
        fprintf(stderr, "  [DPFlate/FLATE] OLD compress done: %zu\n", old_compressed.size());
        fflush(stderr);
        if (old_compressed.empty()) {
            fprintf(stderr, "  [DPFlate/FLATE] FAIL OLD compress size=%zu flag=%d\n",
                    corpus_size, use_flag ? 1 : 0);
            return false;
        }
        if (old_compressed != mem_result.compressed) {
            fprintf(stderr,
                    "  [DPFlate/FLATE] FAIL OLD!=NEW size=%zu flag=%d old=%zu new=%zu\n",
                    corpus_size, use_flag ? 1 : 0,
                    old_compressed.size(), mem_result.compressed.size());
            return false;
        }
    }

    fprintf(stderr,
            "  [DPFlate/FLATE] PASS size=%zu flag=%d payload=%zu (OLD)\n",
            corpus_size, use_flag ? 1 : 0, mem_result.compressed.size());
    return true;
}

// ──── DPFlate 3HfMT ────

bool test_dpflate_3hm(size_t corpus_size, bool use_flag) {
    using compressor::algorithm::DPFlateConfig;
    using compressor::algorithm::compress_bytes_dpflate;
    using compressor::algorithm::decompress_bytes_dpflate;
    using compressor::algorithm::pipeline::DPFlateStreamingPipeline;
    using compressor::algorithm::pipeline::DPFlateStreamingOptions;

    const auto data = make_corpus(corpus_size);

    DPFlateConfig cfg(4095, 255, 3, use_flag, true, 0, 4);

    auto mem_result = compress_bytes_dpflate(data, cfg);
    if (mem_result.compressed.empty()) {
        fprintf(stderr, "  [DPFlate/3HfMT] FAIL mem compress size=%zu flag=%d\n",
                corpus_size, use_flag ? 1 : 0);
        return false;
    }

    auto dec_mem = decompress_bytes_dpflate(mem_result.compressed, cfg);
    if (dec_mem != data) {
        fprintf(stderr, "  [DPFlate/3HfMT] FAIL B-mem size=%zu flag=%d dec=%zu\n",
                corpus_size, use_flag ? 1 : 0, dec_mem.size());
        return false;
    }

    {
        auto dec_wcx_fn = [&](const std::vector<uint8_t>& c) {
            return decompress_bytes_dpflate(c, cfg);
        };
        if (!check_wcx_roundtrip(mem_result.compressed, data, dec_wcx_fn, 5)) {
            fprintf(stderr, "  [DPFlate/3HfMT] FAIL WCX size=%zu flag=%d\n",
                    corpus_size, use_flag ? 1 : 0);
            return false;
        }
    }

const fs::path work = fs::temp_directory_path() / "dpflate_3hm_cartesian";
    std::error_code ec;
    fs::create_directories(work, ec);

    const fs::path in_path = work / "in.bin";
    const fs::path out_path = work / "out.wcx";
    write_all(in_path, data);

    DPFlateStreamingOptions opts;
    opts.chunk_size = kChunkSize;
    opts.workspace_dir = work.string();

    DPFlateStreamingPipeline pipeline(cfg, opts);
    pipeline.compress_file(in_path.string(), out_path.string());

    auto stream_compressed = read_all(out_path);
    if (stream_compressed.empty()) {
        fprintf(stderr, "  [DPFlate/3HfMT] FAIL stream compress size=%zu flag=%d\n",
                corpus_size, use_flag ? 1 : 0);
        fs::remove_all(work, ec);
        return false;
    }

    auto dec_stream = decompress_bytes_dpflate(stream_compressed, cfg);
    if (dec_stream != data) {
        fprintf(stderr, "  [DPFlate/3HfMT] FAIL B-stream size=%zu flag=%d dec=%zu\n",
                corpus_size, use_flag ? 1 : 0, dec_stream.size());
        fs::remove_all(work, ec);
        return false;
    }

    if (mem_result.compressed != stream_compressed) {
        fprintf(stderr,
                "  [DPFlate/3HfMT] FAIL A mem!=stream size=%zu flag=%d mem=%zu stream=%zu\n",
                corpus_size, use_flag ? 1 : 0,
                mem_result.compressed.size(), stream_compressed.size());
        fs::remove_all(work, ec);
        return false;
    }

    // === OLD vs NEW comparison ===
    {
        auto old_compressed = test_bridge::old_dpflate_compress(
            data, 4095, 255, 4, 3, 6, true, 8);
        if (old_compressed.empty()) {
            fprintf(stderr, "  [DPFlate/3HfMT] FAIL OLD compress size=%zu flag=%d\n",
                    corpus_size, use_flag ? 1 : 0);
            return false;
        }
        if (old_compressed != mem_result.compressed) {
            fprintf(stderr,
                    "  [DPFlate/3HfMT] FAIL OLD!=NEW size=%zu flag=%d old=%zu new=%zu\n",
                    corpus_size, use_flag ? 1 : 0,
                    old_compressed.size(), mem_result.compressed.size());
            size_t min_sz = old_compressed.size() < mem_result.compressed.size()
                ? old_compressed.size() : mem_result.compressed.size();
            for (size_t i = 0; i < min_sz; ++i) {
                if (old_compressed[i] != mem_result.compressed[i]) {
                    fprintf(stderr, "  [DPFlate/3HfMT] DIFF at byte %zu: OLD=0x%02x NEW=0x%02x\n",
                            i, old_compressed[i], mem_result.compressed[i]);
                    size_t ctx_start = (i > 16) ? (i - 16) : 0;
                    size_t ctx_end = (i + 16 < min_sz) ? (i + 16) : min_sz;
                    fprintf(stderr, "  [DPFlate/3HfMT] OLD context [%zu..%zu]: ", ctx_start, ctx_end);
                    for (size_t j = ctx_start; j <= ctx_end && j < min_sz; ++j) {
                        fprintf(stderr, "%02x ", old_compressed[j]);
                    }
                    fprintf(stderr, "\n");
                    fprintf(stderr, "  [DPFlate/3HfMT] NEW context [%zu..%zu]: ", ctx_start, ctx_end);
                    for (size_t j = ctx_start; j <= ctx_end && j < min_sz; ++j) {
                        fprintf(stderr, "%02x ", mem_result.compressed[j]);
                    }
                    fprintf(stderr, "\n");
                }
            }
            if (old_compressed.size() < mem_result.compressed.size()) {
                fprintf(stderr, "  [DPFlate/3HfMT] NEW extra bytes [%zu..%zu]: ",
                        old_compressed.size(), mem_result.compressed.size() - 1);
                for (size_t i = old_compressed.size(); i < mem_result.compressed.size(); ++i) {
                    fprintf(stderr, "%02x ", mem_result.compressed[i]);
                }
                fprintf(stderr, "\n");
            }
            return false;
        }
    }

    fprintf(stderr,
            "  [DPFlate/3HfMT] PASS size=%zu flag=%d payload=%zu (OLD)\n",
            corpus_size, use_flag ? 1 : 0, mem_result.compressed.size());
    return true;
}

}  // namespace

int main() {
    const bool quick = env_truthy("CARTESIAN_QUICK");

    fprintf(stderr,
            "[deflate_dpflate_cartesian] "
            "sizes=64/256/512/2048 KiB chunk=%zu KiB quick=%d\n",
            kChunkSize / 1024, quick ? 1 : 0);

    const size_t n_sizes = quick ? 1u : (sizeof(kCorpusSizes) / sizeof(kCorpusSizes[0]));

    bool ok = true;
    int passed = 0;
    int total = 0;

    // ──── Deflate FLATE (F1-F3, F1b-F3b) ────
    fprintf(stderr, "\n=== Deflate FLATE ===\n");
    for (size_t i = 0; i < n_sizes; ++i) {
        for (const bool use_flag : {true, false}) {
            ++total;
            if (test_deflate_flate(kCorpusSizes[i], use_flag)) {
                ++passed;
            } else {
                ok = false;
            }
        }
    }

    // ──── Deflate 3HfMT (F4-F9) ────
    fprintf(stderr, "\n=== Deflate 3HfMT ===\n");
    for (size_t i = 0; i < n_sizes; ++i) {
        for (const bool use_flag : {false, true}) {
            ++total;
            if (test_deflate_3hm(kCorpusSizes[i], use_flag)) {
                ++passed;
            } else {
                ok = false;
            }
        }
    }

    // ──── DPFlate FLATE (P1-P6) ────
    fprintf(stderr, "\n=== DPFlate FLATE ===\n");
    for (size_t i = 0; i < n_sizes; ++i) {
        for (const bool use_flag : {false, true}) {
            ++total;
            if (test_dpflate_flate(kCorpusSizes[i], use_flag)) {
                ++passed;
            } else {
                ok = false;
            }
        }
    }

    // ──── DPFlate 3HfMT (P7-P12) ────
    fprintf(stderr, "\n=== DPFlate 3HfMT ===\n");
    for (size_t i = 0; i < n_sizes; ++i) {
        for (const bool use_flag : {false, true}) {
            ++total;
            if (test_dpflate_3hm(kCorpusSizes[i], use_flag)) {
                ++passed;
            } else {
                ok = false;
            }
        }
    }

    fprintf(stderr, "\n[deflate_dpflate_cartesian] %d/%d PASS — %s\n",
            passed, total, ok ? "ALL PASS" : "FAIL");
    return ok ? 0 : 1;
}
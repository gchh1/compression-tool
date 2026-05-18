/// D1/D2 Cartesian sub-matrix: chunks 30/57/97/113 KiB × corpora 256/522/1025/2000 KiB.
/// Condition A: ``LZDPCompressor``（``compress_dp``）裸流 ≡ 精确 ``Pipeline`` push 输出（§2.2）。
/// Condition B: ``compressFile`` → ``decompressFile`` roundtrip.
/// Debug: ``WEBCOMPRESS_LZDP_STREAM_DEBUG=Package/logs/lzdp_stream_containers.log``
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
#include "LZDP.hpp"
#include "LZDPCompressor.hpp"
#include "LZDPStreamDebug.hpp"
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

compressor::core::LzdpWholeFileParams default_wf(bool use_flag) {
    compressor::core::LzdpWholeFileParams wf{};
    wf.search_size = 4096;
    wf.lookahead_size = 256;
    wf.min_match = 4;
    wf.dp_top = 3;
    wf.use_flag_encoding = use_flag;
    wf.match_engine = 0;
    return wf;
}

std::vector<uint8_t> compress_memory(const std::vector<uint8_t>& data,
                                     const compressor::core::LzdpWholeFileParams& wf) {
    compressor::core::LZDPCompressor mem;
    mem.set_search_size(wf.search_size);
    mem.set_lookahead_size(wf.lookahead_size);
    mem.set_min_match(wf.min_match);
    mem.set_dp_top(wf.dp_top);
    mem.set_use_flag_encoding(wf.use_flag_encoding);
    mem.set_match_engine(wf.match_engine);
    const auto cr = mem.compress(data);
    if (!cr.success) {
        return {};
    }
    return cr.data;
}

std::vector<uint8_t> compress_stream_exact(const std::vector<uint8_t>& data,
                                           const compressor::core::LzdpWholeFileParams& wf,
                                           size_t stream_chunk) {
    compressor::algorithm::LZDPStreamDebug::init_once();
    if (compressor::algorithm::LZDPStreamDebug::enabled()) {
        compressor::algorithm::LZDPStreamDebug::arm_session(
            static_cast<uint32_t>(data.size()));
    }

    auto enc = std::make_unique<compressor::algorithm::LZDP_Streaming>(
        wf.search_size, wf.lookahead_size, wf.min_match, wf.dp_top, wf.use_flag_encoding,
        wf.match_engine);

    const size_t out_pool =
        compressor::processor::pipeline_output_pool_chunk_bytes(stream_chunk);
    auto pool = std::make_shared<compressor::memory::MemoryPool>(8, out_pool);
    std::vector<std::unique_ptr<compressor::algorithm::IAlgorithm>> algos;
    algos.push_back(std::move(enc));
    compressor::processor::Pipeline pipeline(std::move(algos), pool);

    std::vector<uint8_t> out;
    out.reserve(data.size() / 2 + 65536);

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
    const auto wf = default_wf(use_flag);
    const auto data = corpus_repeat(file_bytes);
    const auto mem = compress_memory(data, wf);
    const auto stream = compress_stream_exact(data, wf, chunk_bytes);
    if (mem.empty()) {
        fprintf(stderr, "[lzdp_cartesian] A FAIL mem compress size=%zu chunk=%zu flag=%d\n",
                file_bytes, chunk_bytes, use_flag ? 1 : 0);
        return false;
    }
    if (mem != stream) {
        fprintf(stderr,
                "[lzdp_cartesian] A FAIL mem!=stream size=%zu chunk=%zu flag=%d mem=%zu stream=%zu\n",
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
    fprintf(stderr,
            "[lzdp_cartesian] A PASS size=%zu chunk=%zu flag=%d payload=%zu (mem==stream)\n",
            file_bytes, chunk_bytes, use_flag ? 1 : 0, mem.size());
    return true;
}

bool condition_b(const fs::path& work, size_t file_bytes, size_t chunk_bytes, bool use_flag) {
    using compressor::api::AlgorithmID;
    using compressor::api::compressFile;
    using compressor::api::decompressFile;

    const auto wf = default_wf(use_flag);
    const auto data = corpus_repeat(file_bytes);
    const fs::path in = work / ("in_" + std::to_string(file_bytes) + ".bin");
    const fs::path wcx = work / ("out_" + std::to_string(file_bytes) + "_" +
                                 std::to_string(chunk_bytes) + ".wcx");
    const fs::path dec = work / ("dec_" + std::to_string(file_bytes) + ".bin");
    write_all(in, data);

    const AlgorithmID ch[] = {AlgorithmID::LZDP};
    const size_t eff_chunk = compressor::processor::effective_stream_chunk_bytes(chunk_bytes);
    auto cr = compressFile(in.string(), wcx.string(), ch, eff_chunk,
                           compressor::core::kFileCompressLzdpWholeFileFramed, &wf, nullptr);
    if (!cr.success) {
        fprintf(stderr,
                "[lzdp_cartesian] B FAIL compress size=%zu req_chunk=%zu eff=%zu flag=%d: %s\n",
                file_bytes, chunk_bytes, eff_chunk, use_flag ? 1 : 0, cr.error_message.c_str());
        return false;
    }
    const AlgorithmID de[] = {AlgorithmID::LZDPDecompress};
    auto dr = decompressFile(wcx.string(), dec.string(), de, eff_chunk);
    if (!dr.success) {
        fprintf(stderr, "[lzdp_cartesian] B FAIL decompress size=%zu chunk=%zu flag=%d: %s\n",
                file_bytes, eff_chunk, use_flag ? 1 : 0, dr.error_message.c_str());
        return false;
    }
    const auto got = read_all(dec);
    if (got != data) {
        fprintf(stderr, "[lzdp_cartesian] B FAIL roundtrip size=%zu chunk=%zu flag=%d dec=%zu\n",
                file_bytes, eff_chunk, use_flag ? 1 : 0, got.size());
        return false;
    }
    fprintf(stderr, "[lzdp_cartesian] B PASS size=%zu chunk=%zu eff=%zu flag=%d comp=%zu\n",
            file_bytes, chunk_bytes, eff_chunk, use_flag ? 1 : 0, cr.compressed_size);
    return true;
}

}  // namespace

int main() {
#ifdef _WIN32
    _putenv_s("WEBCOMPRESS_STREAM_CHUNK_MIN_BYTES", "30720");
#else
    setenv("WEBCOMPRESS_STREAM_CHUNK_MIN_BYTES", "30720", 1);
#endif

    if (const char* dbg = std::getenv("WEBCOMPRESS_LZDP_STREAM_DEBUG")) {
        fprintf(stderr, "[lzdp_cartesian] stream debug -> %s\n",
                (dbg[0] == '1' && dbg[1] == '\0') ? "lzdp_stream_containers.log" : dbg);
        compressor::algorithm::LZDPStreamDebug::init_once();
    }

    const bool skip_b = env_truthy("CARTESIAN_SKIP_B");
    const bool quick = env_truthy("CARTESIAN_QUICK");

    fs::path work = fs::temp_directory_path() / "lzdp_cartesian_stream_matrix_test";
    if (const char* w = std::getenv("WEBCOMPRESS_TEST_WORKDIR"); w && w[0]) {
        work = fs::path(w) / "lzdp_cartesian_stream_matrix_test";
    }
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);

    fprintf(stderr,
            "[lzdp_cartesian] D1/D2 chunks=30/57/97/113 KiB sizes=256/522/1025/2000 KiB "
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
    fprintf(stderr, "[lzdp_cartesian] %s\n", ok ? "ALL PASS" : "FAIL");
    return ok ? 0 : 1;
}

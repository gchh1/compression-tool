/// 小文件 / 小流式分块回归：67 KiB 语料 × 7 KiB Pipeline/compressFile chunk。
/// 覆盖矩阵 P1/P7（DPFlate）、D1（LZDP），以及 BUG-01（Deflate 3HfM 内存解压）、BUG-07（3HfM chunk_bits）。
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <vector>

#include "DPFlate.hpp"
#include "DPFlateCompressor.hpp"
#include "DeflateCompressor.hpp"
#include "IAlgorithm.hpp"
#include "LZDP.hpp"
#include "LZDPCompressor.hpp"
#include "MemoryPool.hpp"
#include "Pipeline.hpp"
#include "StreamChunkPolicy.hpp"
#include "api.hpp"

namespace fs = std::filesystem;

namespace {

constexpr size_t kFileBytes = 67u * 1024u;
constexpr size_t kChunkBytes = 7u * 1024u;

std::vector<uint8_t> corpus_67k() {
    std::vector<uint8_t> d(kFileBytes);
    const uint8_t pat[] = "SMALL_STREAM_7K_67K_REGRESS_";
    for (size_t i = 0; i < kFileBytes; ++i) {
        d[i] = pat[i % (sizeof(pat) - 1)];
    }
    return d;
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

std::vector<uint8_t> dpflate_stream_exact(const std::vector<uint8_t>& data,
                                          const compressor::core::DpflatePipelineParams& df) {
    const size_t min_m = df.min_match == 0 ? size_t{4} : df.min_match;
    auto enc = std::make_unique<compressor::algorithm::DPFlate>(
        df.search_size, df.lookahead_size, min_m, df.max_chain_length, df.dp_sub_match_max);
    enc->set_match_engine(static_cast<int>(df.match_engine));
    enc->set_use_flag_encoding(df.use_flag_encoding);
    enc->set_use_3hfmtree(df.use_3hfmtree);
    enc->set_huffman_offset_chunk_bits(df.huffman_offset_chunk_bits);
    enc->set_huffman_length_chunk_bits(df.huffman_length_chunk_bits);

    const size_t out_pool =
        compressor::processor::pipeline_output_pool_chunk_bytes(kChunkBytes);
    auto pool = std::make_shared<compressor::memory::MemoryPool>(8, out_pool);
    std::vector<std::unique_ptr<compressor::algorithm::IAlgorithm>> algos;
    algos.push_back(std::move(enc));
    compressor::processor::Pipeline pipeline(std::move(algos), pool);

    std::vector<uint8_t> out;
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
        const size_t n = (std::min)(kChunkBytes, data.size() - off);
        const bool last = (off + n >= data.size());
        pipeline.push(std::span<const uint8_t>(data.data() + off, n), last);
        off += n;
        drain();
    }
    pipeline.finish();
    drain();
    return out;
}

bool check_mem_eq_stream(const char* label, const std::vector<uint8_t>& mem,
                         const std::vector<uint8_t>& stream) {
    if (mem == stream) {
        fprintf(stderr, "[%s] A PASS mem==stream payload=%zu\n", label, mem.size());
        return true;
    }
    fprintf(stderr, "[%s] A FAIL mem=%zu stream=%zu\n", label, mem.size(), stream.size());
    const size_t n = (std::min)(mem.size(), stream.size());
    for (size_t i = 0; i < n; ++i) {
        if (mem[i] != stream[i]) {
            fprintf(stderr, "  first diff @%zu mem=0x%02x stream=0x%02x\n", i, mem[i], stream[i]);
            break;
        }
    }
    return false;
}

compressor::core::DpflatePipelineParams dpflate_params(bool use_3hm, bool use_flag,
                                                       uint8_t huff_chunk_bits = 8) {
    compressor::core::DpflatePipelineParams df{};
    df.search_size = 4096;
    df.lookahead_size = 256;
    df.min_match = 4;
    df.max_chain_length = 256;
    df.dp_sub_match_max = 6;
    df.match_engine = 1;
    df.use_flag_encoding = use_flag;
    df.use_3hfmtree = use_3hm;
    df.huffman_offset_chunk_bits = huff_chunk_bits;
    df.huffman_length_chunk_bits = huff_chunk_bits;
    return df;
}

bool test_dpflate_row(const char* label, const std::vector<uint8_t>& data, bool use_3hm,
                      bool use_flag) {
    const auto df = dpflate_params(use_3hm, use_flag);

    compressor::core::DPFlateCompressor mem;
    mem.set_search_size(df.search_size);
    mem.set_lookahead_size(df.lookahead_size);
    mem.set_min_match(df.min_match);
    mem.set_max_chain_length(df.max_chain_length);
    mem.set_dp_sub_match_max(df.dp_sub_match_max);
    mem.set_match_engine(static_cast<int>(df.match_engine));
    mem.set_use_flag_encoding(df.use_flag_encoding);
    mem.set_use_3hfmtree(use_3hm);
    mem.set_huffman_offset_chunk_bits(df.huffman_offset_chunk_bits);
    mem.set_huffman_length_chunk_bits(df.huffman_length_chunk_bits);
    const auto mem_cr = mem.compress(data);
    if (!mem_cr.success) {
        fprintf(stderr, "[%s] memory compress failed\n", label);
        return false;
    }

    const auto stream_out = dpflate_stream_exact(data, df);
    if (!check_mem_eq_stream(label, mem_cr.data, stream_out)) {
        return false;
    }

    using compressor::api::AlgorithmID;
    using compressor::api::compressFile;
    using compressor::api::decompressFile;
    const fs::path work = fs::temp_directory_path() / "small_7k_67k_dpflate";
    fs::create_directories(work);
    const fs::path in = work / "in.bin";
    const fs::path wcx = work / "out.wcx";
    const fs::path dec = work / "dec.bin";
    write_all(in, data);

    const AlgorithmID ch[] = {AlgorithmID::DPFlate};
    const size_t eff = compressor::processor::effective_stream_chunk_bytes(kChunkBytes);
    auto cr = compressFile(in.string(), wcx.string(), ch, eff,
                           compressor::core::kFileCompressOptsNone, nullptr, &df);
    if (!cr.success) {
        fprintf(stderr, "[%s] B FAIL compressFile: %s\n", label, cr.error_message.c_str());
        return false;
    }
    const AlgorithmID de[] = {AlgorithmID::Inflate};
    auto dr = decompressFile(wcx.string(), dec.string(), de, eff);
    if (!dr.success) {
        fprintf(stderr, "[%s] B FAIL decompressFile: %s\n", label, dr.error_message.c_str());
        return false;
    }
    const auto got = read_all(dec);
    if (got != data) {
        fprintf(stderr, "[%s] B FAIL roundtrip dec=%zu\n", label, got.size());
        return false;
    }
    fprintf(stderr, "[%s] B PASS roundtrip comp=%zu chunk_eff=%zu\n", label, cr.compressed_size,
            eff);
    return true;
}

bool test_lzdp_d1(const std::vector<uint8_t>& data, bool use_flag) {
    const char* label = use_flag ? "D2" : "D1";
    compressor::core::LzdpWholeFileParams wf{};
    wf.search_size = 4096;
    wf.lookahead_size = 256;
    wf.min_match = 4;
    wf.dp_top = 3;
    wf.use_flag_encoding = use_flag;
    wf.match_engine = 0;

    compressor::core::LZDPCompressor mem;
    mem.set_search_size(wf.search_size);
    mem.set_lookahead_size(wf.lookahead_size);
    mem.set_min_match(wf.min_match);
    mem.set_dp_top(wf.dp_top);
    mem.set_use_flag_encoding(wf.use_flag_encoding);
    mem.set_match_engine(wf.match_engine);
    const auto mem_cr = mem.compress(data);
    if (!mem_cr.success) {
        fprintf(stderr, "[%s] memory failed\n", label);
        return false;
    }

    auto enc = std::make_unique<compressor::algorithm::LZDP_Streaming>(
        wf.search_size, wf.lookahead_size, wf.min_match, wf.dp_top, wf.use_flag_encoding,
        wf.match_engine);
    const size_t out_pool =
        compressor::processor::pipeline_output_pool_chunk_bytes(kChunkBytes);
    auto pool = std::make_shared<compressor::memory::MemoryPool>(8, out_pool);
    std::vector<std::unique_ptr<compressor::algorithm::IAlgorithm>> algos;
    algos.push_back(std::move(enc));
    compressor::processor::Pipeline pipeline(std::move(algos), pool);
    std::vector<uint8_t> stream_out;
    auto drain = [&]() {
        for (;;) {
            auto c = pipeline.pull();
            if (c.empty()) {
                break;
            }
            const auto v = c.view();
            stream_out.insert(stream_out.end(), v.begin(), v.end());
        }
    };
    for (size_t off = 0; off < data.size();) {
        const size_t n = (std::min)(kChunkBytes, data.size() - off);
        const bool last = (off + n >= data.size());
        pipeline.push(std::span<const uint8_t>(data.data() + off, n), last);
        off += n;
        drain();
    }
    pipeline.finish();
    drain();

    if (!check_mem_eq_stream(label, mem_cr.data, stream_out)) {
        return false;
    }

    using compressor::api::AlgorithmID;
    using compressor::api::compressFile;
    using compressor::api::decompressFile;
    const fs::path work = fs::temp_directory_path() / "small_7k_67k_lzdp";
    fs::create_directories(work);
    const fs::path in = work / "in.bin";
    const fs::path wcx = work / "out.wcx";
    const fs::path dec = work / "dec.bin";
    write_all(in, data);
    const AlgorithmID ch[] = {AlgorithmID::LZDP};
    const size_t eff = compressor::processor::effective_stream_chunk_bytes(kChunkBytes);
    auto cr = compressFile(in.string(), wcx.string(), ch, eff,
                           compressor::core::kFileCompressLzdpWholeFileFramed, &wf, nullptr);
    if (!cr.success || decompressFile(wcx.string(), dec.string(),
                                      (const AlgorithmID[]){AlgorithmID::LZDPDecompress}, eff)
                               .success == false) {
        fprintf(stderr, "[%s] B FAIL file path\n", label);
        return false;
    }
    if (read_all(dec) != data) {
        fprintf(stderr, "[%s] B FAIL roundtrip\n", label);
        return false;
    }
    fprintf(stderr, "[%s] B PASS roundtrip\n", label);
    return true;
}

bool test_bug01_deflate_3hm(const std::vector<uint8_t>& data) {
    fprintf(stderr, "[BUG-01] Deflate 3HfM memory compress+decompress (%zu B)...\n",
            data.size());
    compressor::core::DeflateCompressor comp;
    comp.set_use_3hfmtree(true);
    comp.set_huffman_chunk_bits(8);
    const auto cr = comp.compress(data);
    if (!cr.success) {
        fprintf(stderr, "[BUG-01] compress failed\n");
        return false;
    }
    const auto dr = comp.decompress(cr.data);
    if (!dr.success || dr.data != data) {
        fprintf(stderr, "[BUG-01] FAIL decompress size=%zu\n", dr.data.size());
        return false;
    }
    fprintf(stderr, "[BUG-01] PASS memory roundtrip comp=%zu\n", cr.data.size());
    return true;
}

bool test_bug07_chunk_bits(const std::vector<uint8_t>& data) {
    bool ok = true;
    for (const int bw : {11, 13, 15}) {
        fprintf(stderr, "[BUG-07] 3HfM huffman_chunk_bits=%d ...\n", bw);
        compressor::core::DPFlateCompressor comp;
        comp.set_search_size(4096);
        comp.set_lookahead_size(256);
        comp.set_min_match(4);
        comp.set_max_chain_length(256);
        comp.set_dp_sub_match_max(6);
        comp.set_match_engine(1);
        comp.set_use_flag_encoding(false);
        comp.set_use_3hfmtree(true);
        comp.set_huffman_chunk_bits(static_cast<uint8_t>(bw));
        const auto cr = comp.compress(data);
        if (!cr.success) {
            fprintf(stderr, "[BUG-07] bw=%d compress failed\n", bw);
            ok = false;
            continue;
        }
        const auto dr = comp.decompress(cr.data);
        if (!dr.success || dr.data != data) {
            fprintf(stderr, "[BUG-07] bw=%d FAIL dec=%zu\n", bw, dr.data.size());
            ok = false;
        } else {
            fprintf(stderr, "[BUG-07] bw=%d PASS\n", bw);
        }
    }
    return ok;
}

}  // namespace

int main() {
#ifdef _WIN32
    _putenv_s("WEBCOMPRESS_STREAM_CHUNK_MIN_BYTES", "7168");
#else
    setenv("WEBCOMPRESS_STREAM_CHUNK_MIN_BYTES", "7168", 1);
#endif

    const auto data = corpus_67k();
    fprintf(stderr,
            "[7k_67k] file=%zu B (67 KiB) chunk=%zu B (7 KiB) pushes~%zu floor=%zu\n",
            data.size(), kChunkBytes, (data.size() + kChunkBytes - 1) / kChunkBytes,
            compressor::processor::stream_chunk_floor_bytes());

    bool ok = true;
    ok = test_dpflate_row("P1", data, false, false) && ok;
    ok = test_dpflate_row("P7", data, true, false) && ok;
    ok = test_dpflate_row("P8", data, true, true) && ok;
    ok = test_lzdp_d1(data, false) && ok;
    ok = test_lzdp_d1(data, true) && ok;
    ok = test_bug01_deflate_3hm(data) && ok;
    ok = test_bug07_chunk_bits(data) && ok;

    fprintf(stderr, "[7k_67k] %s\n", ok ? "ALL PASS" : "FAIL");
    return ok ? 0 : 1;
}

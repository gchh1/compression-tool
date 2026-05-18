/// 流式 LZDP / DPFlate（FLATE 与 3HfM）压缩与解压路径（compressFile / decompressFile）。
/// P1/P2/P7/P8 条件 A：流式 WCX payload 与 DPFlateCompressor 内存输出逐字节一致（chunk=300KiB）。
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include <memory>
#include <span>

#include "AlgorithmFactory.hpp"
#include "DPFlateCompressor.hpp"
#include "LZDP.hpp"
#include "LZDPCompressor.hpp"
#include "MemoryPool.hpp"
#include "Pipeline.hpp"
#include "StreamChunkPolicy.hpp"
#include "api.hpp"

namespace fs = std::filesystem;

namespace {

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

std::vector<uint8_t> corpus_pattern() {
    const std::string pat = "REGRESS_LZDP_DPFLATE_";
    std::vector<uint8_t> d;
    constexpr int kRepeats = 32;
    d.reserve(pat.size() * kRepeats);
    for (int i = 0; i < kRepeats; ++i) {
        d.insert(d.end(), pat.begin(), pat.end());
    }
    return d;
}

std::vector<uint8_t> corpus_random() {
    std::vector<uint8_t> r(384);
    std::mt19937 gen(12345);
    for (auto& b : r) {
        b = static_cast<uint8_t>(gen() & 0xFF);
    }
    return r;
}

std::vector<uint8_t> corpus_text() {
    const std::string text =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
        "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. ";
    std::vector<uint8_t> d;
    while (d.size() < 22300) {
        d.insert(d.end(), text.begin(), text.end());
    }
    return d;
}

std::vector<uint8_t> corpus_binary_64k() {
    std::vector<uint8_t> d(65536);
    std::mt19937 gen(67890);
    for (auto& b : d) {
        b = static_cast<uint8_t>(gen() & 0xFF);
    }
    return d;
}

bool lzdp_stream_roundtrip(const fs::path& work, const std::vector<uint8_t>& data,
                           const compressor::core::LzdpWholeFileParams& wf) {
    using compressor::api::AlgorithmID;
    using compressor::api::compressFile;
    using compressor::api::decompressFile;
    const fs::path in = work / "in.bin";
    const fs::path wcx = work / "out.wcx";
    const fs::path dec = work / "dec.bin";
    write_all(in, data);
    const AlgorithmID ch[] = {AlgorithmID::LZDP};
    auto cr = compressFile(in.string(), wcx.string(), ch, 8192u,
                           compressor::core::kFileCompressLzdpWholeFileFramed, &wf, nullptr);
    if (!cr.success) {
        std::cerr << "[lzdp-stream] compressFile: " << cr.error_message << "\n";
        return false;
    }
    const AlgorithmID de[] = {AlgorithmID::LZDPDecompress};
    auto dr = decompressFile(wcx.string(), dec.string(), de, 8192u);
    if (!dr.success) {
        std::cerr << "[lzdp-stream] decompressFile: " << dr.error_message << "\n";
        return false;
    }
    const auto got = read_all(dec);
    if (got.size() != data.size()) {
        std::cerr << "[lzdp-stream] size mismatch dec=" << got.size() << " orig=" << data.size()
                  << "\n";
        return false;
    }
    if (got != data) {
        for (size_t i = 0; i < got.size(); ++i) {
            if (got[i] != data[i]) {
                std::cerr << "[lzdp-stream] byte diff at " << i << " orig=0x" << std::hex
                          << static_cast<unsigned>(data[i]) << " dec=0x"
                          << static_cast<unsigned>(got[i]) << std::dec << "\n";
                break;
            }
        }
        return false;
    }
    return true;
}

bool dpflate_stream_flate_roundtrip(const fs::path& work, const std::vector<uint8_t>& data,
                                    compressor::core::DpflatePipelineParams& df) {
    using compressor::api::AlgorithmID;
    using compressor::api::compressFile;
    using compressor::api::decompressFile;
    df.use_3hfmtree = false;
    df.huffman_offset_chunk_bits = 8;
    df.huffman_length_chunk_bits = 8;
    const fs::path in = work / "in_dpf.bin";
    const fs::path wcx = work / "out_dpf.wcx";
    const fs::path dec = work / "dec_dpf.bin";
    write_all(in, data);
    const AlgorithmID ch[] = {AlgorithmID::DPFlate};
    auto cr = compressFile(in.string(), wcx.string(), ch, 8192u,
                           compressor::core::kFileCompressOptsNone, nullptr, &df);
    if (!cr.success) {
        std::cerr << "[dpflate-stream-flate] compress: " << cr.error_message << "\n";
        return false;
    }
    const AlgorithmID de[] = {AlgorithmID::Inflate};
    auto dr = decompressFile(wcx.string(), dec.string(), de, 8192u);
    if (!dr.success) {
        std::cerr << "[dpflate-stream-flate] decompress: " << dr.error_message << "\n";
        return false;
    }
    return read_all(dec) == data;
}

bool dpflate_stream_vs_memory_payload(const fs::path& work, const std::vector<uint8_t>& data,
                                      compressor::core::DpflatePipelineParams& df,
                                      bool use_3hm) {
    using compressor::api::AlgorithmID;
    using compressor::api::compressFile;
    using compressor::api::unpack_wcx;

    df.use_3hfmtree = use_3hm;
    df.huffman_offset_chunk_bits = 8;
    df.huffman_length_chunk_bits = 8;
    df.use_flag_encoding = false;

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
        std::cerr << "[dpflate-parity] memory compress: " << mem_cr.error_message << "\n";
        return false;
    }
    const auto& mem_out = mem_cr.data;

    const fs::path in = work / "parity_in.bin";
    const fs::path wcx = work / "parity_out.wcx";
    write_all(in, data);
    const AlgorithmID ch[] = {AlgorithmID::DPFlate};
    constexpr size_t kChunk = 300u * 1024u;
    auto cr = compressFile(in.string(), wcx.string(), ch, kChunk,
                           compressor::core::kFileCompressOptsNone, nullptr, &df);
    if (!cr.success) {
        std::cerr << "[dpflate-parity] stream compress: " << cr.error_message << "\n";
        return false;
    }
    const auto wcx_bytes = read_all(wcx);
    const auto unpacked = unpack_wcx(wcx_bytes);
    if (!unpacked.success) {
        std::cerr << "[dpflate-parity] unpack_wcx failed\n";
        return false;
    }
    const auto& stream_payload = unpacked.payload;
    if (stream_payload != mem_out) {
        std::cerr << "[dpflate-parity] payload mismatch mem=" << mem_out.size()
                  << " stream=" << stream_payload.size() << " use_3hm=" << use_3hm << "\n";
        const size_t n = (std::min)(mem_out.size(), stream_payload.size());
        for (size_t i = 0; i < n; ++i) {
            if (mem_out[i] != stream_payload[i]) {
                std::cerr << "  first diff @" << i << " mem=0x" << std::hex
                          << static_cast<unsigned>(mem_out[i]) << " stream=0x"
                          << static_cast<unsigned>(stream_payload[i]) << std::dec << "\n";
                break;
            }
        }
        return false;
    }
    return true;
}

bool parity_3hm_corpus(const fs::path& work, const char* name, const std::vector<uint8_t>& data,
                       compressor::core::DpflatePipelineParams& df, bool use_flag) {
    df.use_flag_encoding = use_flag;
    if (!dpflate_stream_vs_memory_payload(work, data, df, true)) {
        fprintf(stderr, "[dpflate-parity] FAIL corpus=%s flag=%d\n", name, use_flag ? 1 : 0);
        return false;
    }
    fprintf(stderr, "[dpflate-parity] PASS corpus=%s flag=%d bytes=%zu\n", name, use_flag ? 1 : 0,
            data.size());
    return true;
}

/// D1 条件 A（§2.2）：内存 ``LZDPCompressor`` 裸流 ≡ 精确 Pipeline push 输出。
bool lzdp_stream_vs_memory_payload(const fs::path& work, const std::vector<uint8_t>& data,
                                   compressor::core::LzdpWholeFileParams& wf,
                                   size_t stream_chunk) {
    (void)work;
    compressor::core::LZDPCompressor mem;
    mem.set_search_size(wf.search_size);
    mem.set_lookahead_size(wf.lookahead_size);
    mem.set_min_match(wf.min_match);
    mem.set_dp_top(wf.dp_top);
    mem.set_use_flag_encoding(wf.use_flag_encoding);
    mem.set_match_engine(wf.match_engine);
    const auto mem_cr = mem.compress(data);
    if (!mem_cr.success) {
        std::cerr << "[lzdp-parity] memory compress failed\n";
        return false;
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

    std::vector<uint8_t> stream_out;
    auto drain = [&]() {
        for (;;) {
            auto chunk = pipeline.pull();
            if (chunk.empty()) {
                break;
            }
            const auto v = chunk.view();
            stream_out.insert(stream_out.end(), v.begin(), v.end());
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

    if (mem_cr.data != stream_out) {
        std::cerr << "[lzdp-parity] mem!=stream bytes=" << data.size()
                  << " mem=" << mem_cr.data.size() << " stream=" << stream_out.size()
                  << " flag=" << wf.use_flag_encoding << "\n";
        const size_t n = (std::min)(mem_cr.data.size(), stream_out.size());
        for (size_t i = 0; i < n; ++i) {
            if (mem_cr.data[i] != stream_out[i]) {
                std::cerr << "  first diff @" << i << " mem=0x" << std::hex
                          << static_cast<unsigned>(mem_cr.data[i]) << " stream=0x"
                          << static_cast<unsigned>(stream_out[i]) << std::dec << "\n";
                break;
            }
        }
        return false;
    }
    fprintf(stderr, "[lzdp-parity] PASS mem==stream bytes=%zu flag=%d chunk=%zu\n", data.size(),
            wf.use_flag_encoding ? 1 : 0, stream_chunk);
    return true;
}

bool dpflate_stream_3hm_compress_ok(const fs::path& work, const std::vector<uint8_t>& data,
                                    compressor::core::DpflatePipelineParams& df) {
    using compressor::api::AlgorithmID;
    using compressor::api::compressFile;
    df.use_3hfmtree = true;
    df.huffman_offset_chunk_bits = 8;
    df.huffman_length_chunk_bits = 8;
    const fs::path in = work / "in_3hm.bin";
    const fs::path wcx1 = work / "out_3hm_a.wcx";
    const fs::path wcx2 = work / "out_3hm_b.wcx";
    write_all(in, data);
    const AlgorithmID ch[] = {AlgorithmID::DPFlate};
    auto c1 = compressFile(in.string(), wcx1.string(), ch, 4096u,
                           compressor::core::kFileCompressOptsNone, nullptr, &df);
    auto c2 = compressFile(in.string(), wcx2.string(), ch, 1024u * 1024u,
                           compressor::core::kFileCompressOptsNone, nullptr, &df);
    if (!c1.success || !c2.success) return false;
    const auto b1 = read_all(wcx1);
    const auto b2 = read_all(wcx2);
    return !b1.empty() && b1 == b2;
}

}  // namespace

int main() {
    using compressor::core::DpflatePipelineParams;
    using compressor::core::LzdpWholeFileParams;

    bool ok = true;
    const fs::path work = fs::temp_directory_path() / "lzdp_dpflate_stream_test";
    std::error_code ec;
    fs::remove_all(work, ec);
    ec.clear();
    fs::create_directories(work, ec);

    LzdpWholeFileParams wf{};
    wf.search_size = 4096;
    wf.lookahead_size = 256;
    wf.min_match = 4;
    wf.dp_top = 3;
    wf.use_flag_encoding = false;
    wf.match_engine = 0;
    if (!lzdp_stream_roundtrip(work, corpus_pattern(), wf)) {
        ok = false;
    }

    DpflatePipelineParams df{};
    df.search_size = 2048;
    df.lookahead_size = 128;
    df.min_match = 4;
    df.max_chain_length = 128;
    df.dp_sub_match_max = 6;
    df.match_engine = 1;
    df.use_flag_encoding = false;

    const auto corp_pat = corpus_pattern();
    if (!dpflate_stream_flate_roundtrip(work, corp_pat, df)) {
        ok = false;
    }
    if (!dpflate_stream_vs_memory_payload(work, corp_pat, df, false)) {
        ok = false;
    }
    df.use_flag_encoding = true;
    if (!dpflate_stream_vs_memory_payload(work, corp_pat, df, false)) {
        ok = false;
    }
    df.use_flag_encoding = false;
    if (!dpflate_stream_3hm_compress_ok(work, corp_pat, df)) {
        ok = false;
    }

    struct NamedCorpus {
        const char* name;
        std::vector<uint8_t> data;
    };
    fprintf(stderr, "[lzdp_dpflate_stream] D1/D2 condition A (LZDP, chunk=300KiB)\n");
    for (const auto& c : std::vector<NamedCorpus>{
             {"pattern", corpus_pattern()},
             {"random", corpus_random()},
             {"text", corpus_text()},
             {"binary_64k", corpus_binary_64k()},
         }) {
        constexpr size_t kChunk = 300u * 1024u;
        wf.use_flag_encoding = false;
        if (!lzdp_stream_vs_memory_payload(work, c.data, wf, kChunk)) {
            ok = false;
        }
        wf.use_flag_encoding = true;
        if (!lzdp_stream_vs_memory_payload(work, c.data, wf, kChunk)) {
            ok = false;
        }
    }

    fprintf(stderr, "[lzdp_dpflate_stream] P7/P8 condition A (3HfM, chunk=300KiB)\n");
    const std::vector<NamedCorpus> parity_corpora = {
        {"pattern", corpus_pattern()},
        {"random", corpus_random()},
        {"text", corpus_text()},
    };
    for (const auto& c : parity_corpora) {
        df.use_flag_encoding = false;
        if (!parity_3hm_corpus(work, c.name, c.data, df, false)) {
            ok = false;
        }
        if (!parity_3hm_corpus(work, c.name, c.data, df, true)) {
            ok = false;
        }
    }
    df.use_flag_encoding = false;
    if (!parity_3hm_corpus(work, "binary_64k", corpus_binary_64k(), df, false)) {
        ok = false;
    }
    if (!parity_3hm_corpus(work, "binary_64k", corpus_binary_64k(), df, true)) {
        ok = false;
    }

    fs::remove_all(work, ec);
    std::cout << (ok ? "test_lzdp_dpflate_stream: PASS\n" : "test_lzdp_dpflate_stream: FAIL\n");
    return ok ? 0 : 1;
}

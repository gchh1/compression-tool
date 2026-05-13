/// 流式 LZDP / DPFlate（FLATE 与 3HfM）压缩与解压路径（compressFile / decompressFile）。
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "AlgorithmFactory.hpp"
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
    // 小语料：避免 Debug+KMP 流式整路径耗时过长，仍覆盖多 token 与窗口逻辑
    constexpr int kRepeats = 32;
    d.reserve(pat.size() * kRepeats);
    for (int i = 0; i < kRepeats; ++i) {
        d.insert(d.end(), pat.begin(), pat.end());
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
    return read_all(dec) == data;
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
    const auto corp = corpus_pattern();
    const fs::path work = fs::temp_directory_path() / "lzdp_dpflate_stream_test";
    std::error_code ec;
    fs::remove_all(work, ec);
    ec.clear();
    fs::create_directories(work, ec);

    LzdpWholeFileParams wf{};
    wf.search_size = 4096;
    wf.lookahead_size = 256;
    wf.min_match = 0;
    wf.dp_top = 3;
    wf.use_flag_encoding = false;
    wf.match_engine = 0;
    if (!lzdp_stream_roundtrip(work, corp, wf)) ok = false;

    DpflatePipelineParams df{};
    df.search_size = 2048;
    df.lookahead_size = 128;
    df.min_match = 4;
    df.max_chain_length = 128;
    df.dp_sub_match_max = 6;
    df.match_engine = 1;
    df.use_flag_encoding = false;
    if (!dpflate_stream_flate_roundtrip(work, corp, df)) ok = false;

    if (!dpflate_stream_3hm_compress_ok(work, corp, df)) ok = false;

    fs::remove_all(work, ec);
    std::cout << (ok ? "test_lzdp_dpflate_stream: PASS\n" : "test_lzdp_dpflate_stream: FAIL\n");
    return ok ? 0 : 1;
}

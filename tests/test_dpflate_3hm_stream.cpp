/// P7/P8: DPFlate + 3HfMT file streaming (compressFile / decompressFile).
/// Chunk size 300 KiB; corpora up to 6 MiB.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "AlgorithmFactory.hpp"
#include "api.hpp"

namespace fs = std::filesystem;

namespace {

constexpr size_t kStreamChunkBytes = 300u * 1024u;
constexpr size_t kMaxCorpusBytes = 6u * 1024u * 1024u;

struct Corpus {
    std::string name;
    std::vector<uint8_t> data;
};

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

Corpus corpus_pattern() {
    const std::string pat = "REGRESS_LZDP_DPFLATE_";
    std::vector<uint8_t> d;
    for (int i = 0; i < 32; ++i) {
        d.insert(d.end(), pat.begin(), pat.end());
    }
    return {"pattern", std::move(d)};
}

Corpus corpus_random() {
    std::vector<uint8_t> r(384);
    std::mt19937 gen(12345);
    for (auto& b : r) {
        b = static_cast<uint8_t>(gen() & 0xFF);
    }
    return {"random", std::move(r)};
}

Corpus corpus_text() {
    const std::string text =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
        "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. ";
    std::vector<uint8_t> d;
    while (d.size() < 22300) {
        d.insert(d.end(), text.begin(), text.end());
    }
    return {"text", std::move(d)};
}

Corpus corpus_binary_64k() {
    std::vector<uint8_t> d(65536);
    std::mt19937 gen(67890);
    for (auto& b : d) {
        b = static_cast<uint8_t>(gen() & 0xFF);
    }
    return {"binary_64k", std::move(d)};
}

Corpus corpus_repeat(const char* name, size_t nbytes) {
    std::vector<uint8_t> d(nbytes);
    const uint8_t pat[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (size_t i = 0; i < nbytes; ++i) {
        d[i] = pat[i % (sizeof(pat) - 1)];
    }
    return {name, std::move(d)};
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

bool roundtrip_stream(const fs::path& work, const Corpus& corp, bool use_flag) {
    using compressor::api::AlgorithmID;
    using compressor::api::compressFile;
    using compressor::api::decompressFile;

    const fs::path in = work / (corp.name + "_in.bin");
    const fs::path wcx = work / (corp.name + "_out.wcx");
    const fs::path dec = work / (corp.name + "_dec.bin");
    write_all(in, corp.data);

    const auto df = default_dp_params(use_flag);
    compressor::core::compression_config().dpflate = df;
    const AlgorithmID ch[] = {AlgorithmID::DPFlate};
    auto cr = compressFile(in.string(), wcx.string(), ch, kStreamChunkBytes);
    if (!cr.success) {
        fprintf(stderr, "[%s] flag=%d compressFile: %s\n", corp.name.c_str(), use_flag ? 1 : 0,
                cr.error_message.c_str());
        return false;
    }

    const AlgorithmID de[] = {AlgorithmID::Inflate};
    auto dr = decompressFile(wcx.string(), dec.string(), de, kStreamChunkBytes);
    if (!dr.success) {
        fprintf(stderr, "[%s] flag=%d decompressFile: %s\n", corp.name.c_str(), use_flag ? 1 : 0,
                dr.error_message.c_str());
        return false;
    }

    const auto got = read_all(dec);
    if (got.size() != corp.data.size()) {
        fprintf(stderr, "[%s] flag=%d size dec=%zu orig=%zu comp=%zu\n", corp.name.c_str(),
                use_flag ? 1 : 0, got.size(), corp.data.size(), cr.compressed_size);
        return false;
    }
    for (size_t i = 0; i < corp.data.size(); ++i) {
        if (got[i] != corp.data[i]) {
            fprintf(stderr, "[%s] flag=%d diff@%zu orig=0x%02x dec=0x%02x\n", corp.name.c_str(),
                    use_flag ? 1 : 0, i, corp.data[i], got[i]);
            return false;
        }
    }
    fprintf(stderr, "[%s] flag=%d PASS orig=%zu comp=%zu (%.1f%%) t_comp=%.0fms t_dec=%.0fms\n",
            corp.name.c_str(), use_flag ? 1 : 0, corp.data.size(), cr.compressed_size,
            corp.data.empty() ? 0.0
                                : 100.0 * cr.compressed_size / static_cast<double>(corp.data.size()),
            cr.time_ms, dr.time_ms);
    return true;
}

}  // namespace

int main() {
    fprintf(stderr, "[dpflate_3hm_stream] P7/P8 chunk=%zu bytes max_corpus=%zu MiB\n",
            kStreamChunkBytes, kMaxCorpusBytes / (1024u * 1024u));

    std::vector<Corpus> corpora;
    corpora.push_back(corpus_pattern());
    corpora.push_back(corpus_random());
    corpora.push_back(corpus_text());
    corpora.push_back(corpus_binary_64k());
    corpora.push_back(corpus_repeat("repeat_1mb", 1u * 1024u * 1024u));
    corpora.push_back(corpus_repeat("repeat_3mb", 3u * 1024u * 1024u));
    corpora.push_back(corpus_repeat("repeat_6mb", kMaxCorpusBytes));

    const fs::path work =
        fs::temp_directory_path() / "dpflate_3hm_stream_test";
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);

    bool ok = true;
    fprintf(stderr, "[dpflate_3hm_stream] --- P7 (flag=false) ---\n");
    for (const auto& c : corpora) {
        if (!roundtrip_stream(work, c, false)) {
            ok = false;
        }
    }

    fprintf(stderr, "[dpflate_3hm_stream] --- P8 (flag=true) ---\n");
    for (const auto& c : corpora) {
        if (!roundtrip_stream(work, c, true)) {
            ok = false;
        }
    }

    fs::remove_all(work, ec);
    fprintf(stderr, "[dpflate_3hm_stream] %s\n", ok ? "ALL PASS" : "FAIL");
    return ok ? 0 : 1;
}

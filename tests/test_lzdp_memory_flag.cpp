/// D4/D6: LZDP memory compress + decompress with use_flag_encoding=true.
/// Matches test_algorithm_comparison test_lzdp_memory params except flag.
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "LZDPCompressor.hpp"

namespace {

struct Corpus {
    std::string name;
    std::vector<uint8_t> data;
};

Corpus corpus_pattern() {
    const std::string pat = "REGRESS_LZDP_DPFLATE_";
    std::vector<uint8_t> d;
    constexpr int kRepeats = 32;
    d.reserve(pat.size() * kRepeats);
    for (int i = 0; i < kRepeats; ++i) {
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
        "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. "
        "Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris "
        "nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in "
        "reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla "
        "pariatur. Excepteur sint occaecat cupidatat non proident, sunt in "
        "culpa qui officia deserunt mollit anim id est laborum. ";
    std::vector<uint8_t> d;
    for (int i = 0; i < 50; ++i) {
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

Corpus corpus_mixed_2mb() {
    const size_t total = 2 * 1024 * 1024;
    std::vector<uint8_t> d;
    d.reserve(total);

    const std::string text =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
        "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. ";
    while (d.size() < total / 3) {
        d.insert(d.end(), text.begin(), text.end());
    }

    const uint8_t pattern[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    while (d.size() < total * 2 / 3) {
        d.push_back(pattern[d.size() % (sizeof(pattern) - 1)]);
    }

    std::mt19937 gen(55555);
    while (d.size() < total) {
        d.push_back(static_cast<uint8_t>(gen() & 0xFF));
    }
    return {"mixed_2mb", std::move(d)};
}

bool roundtrip_memory_flag(const Corpus& corp, bool use_flag) {
    compressor::core::LZDPCompressor comp;
    comp.set_search_size(4096);
    comp.set_lookahead_size(256);
    comp.set_min_match(0);
    comp.set_dp_top(3);
    comp.set_use_flag_encoding(use_flag);
    comp.set_match_engine(0);

    const auto cr = comp.compress(corp.data);
    if (!cr.success) {
        fprintf(stderr, "[%s] flag=%d compress failed\n", corp.name.c_str(), use_flag ? 1 : 0);
        return false;
    }

    const auto dec = comp.decompress(cr.data);
    if (!dec.success) {
        fprintf(stderr, "[%s] flag=%d decompress failed\n", corp.name.c_str(), use_flag ? 1 : 0);
        return false;
    }

    if (dec.data.size() != corp.data.size()) {
        fprintf(stderr, "[%s] flag=%d size mismatch dec=%zu orig=%zu\n", corp.name.c_str(),
                use_flag ? 1 : 0, dec.data.size(), corp.data.size());
        return false;
    }

    for (size_t i = 0; i < corp.data.size(); ++i) {
        if (dec.data[i] != corp.data[i]) {
            fprintf(stderr,
                    "[%s] flag=%d first diff at %zu: orig=0x%02x dec=0x%02x compressed=%zu\n",
                    corp.name.c_str(), use_flag ? 1 : 0, i, corp.data[i], dec.data[i],
                    cr.data.size());
            return false;
        }
    }

    fprintf(stderr, "[%s] flag=%d PASS orig=%zu comp=%zu ratio=%.2f%%\n", corp.name.c_str(),
            use_flag ? 1 : 0, corp.data.size(), cr.data.size(),
            corp.data.empty() ? 0.0
                              : 100.0 * static_cast<double>(cr.data.size()) / corp.data.size());
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    bool include_large = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--large") {
            include_large = true;
        }
    }

    std::vector<Corpus> corpora;
    corpora.push_back(corpus_pattern());
    corpora.push_back(corpus_random());
    corpora.push_back(corpus_text());
    corpora.push_back(corpus_binary_64k());
    if (include_large) {
        corpora.push_back(corpus_mixed_2mb());
    }

    fprintf(stderr, "[lzdp_memory_flag] D4/D6: memory roundtrip use_flag_encoding=true\n");
    bool ok = true;
    for (const auto& c : corpora) {
        if (!roundtrip_memory_flag(c, true)) {
            ok = false;
        }
    }

    fprintf(stderr, "[lzdp_memory_flag] sanity: flag=false (same as D3 path)\n");
    if (!roundtrip_memory_flag(corpus_pattern(), false)) {
        ok = false;
    }

    fprintf(stderr, "[lzdp_memory_flag] %s\n", ok ? "ALL PASS" : "FAIL");
    return ok ? 0 : 1;
}

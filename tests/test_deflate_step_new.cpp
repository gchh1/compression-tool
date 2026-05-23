/**
 * @file test_deflate_step_new.cpp
 * @brief 只链接 algorithm_new，输出新版 Deflate 的 LZ tokens + 压缩字节流
 */
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

#include "Deflate.hpp"
#include "MatchEngine.hpp"

using namespace compressor::algorithm;
using namespace compressor::algorithm::LZMatcher;

struct Corpus {
    const char* name;
    std::vector<uint8_t> data;
};

static std::vector<Corpus> generate_corpora() {
    std::vector<Corpus> corpus;

    {
        const char* pat = "REGRESS_LZDP_DPFLATE_COMPARE_TEST_V1";
        std::vector<uint8_t> d;
        for (int i = 0; i < 16; ++i) {
            d.insert(d.end(), pat, pat + std::strlen(pat));
        }
        corpus.push_back({"pattern_16x", d});
    }

    {
        std::mt19937 gen(42);
        std::vector<uint8_t> d(256);
        for (auto& b : d) b = static_cast<uint8_t>(gen() & 0xFF);
        corpus.push_back({"random_256", d});
    }

    {
        const std::string text =
            "Lorem ipsum dolor sit amet consectetur adipiscing elit "
            "sed do eiusmod tempor incididunt ut labore et dolore magna aliqua "
            "ut enim ad minim veniam quis nostrud exercitation ullamco laboris ";
        std::vector<uint8_t> d;
        for (int i = 0; i < 20; ++i) d.insert(d.end(), text.begin(), text.end());
        corpus.push_back({"latin_20x", d});
    }

    {
        std::mt19937 gen(99);
        std::vector<uint8_t> d(1024);
        for (auto& b : d) b = static_cast<uint8_t>(gen() & 0xFF);
        corpus.push_back({"random_1024", d});
    }

    {
        const char* rep = "AAAAABBBBBCCCCCDDDDDEEEEEFFFFFGGGGGHHHHHIIIIIJJJJJ";
        std::vector<uint8_t> d;
        for (int i = 0; i < 10; ++i) d.insert(d.end(), rep, rep + std::strlen(rep));
        corpus.push_back({"repeated_10x", d});
    }

    return corpus;
}

struct TestParams {
    size_t search_size;
    size_t min_match;
    size_t max_chain_length;
    size_t lookahead_max;
    bool use_flag;
};

static std::vector<TestParams> get_params() {
    return {
        {4096, 3, 256, 258, false},
        {4096, 3, 8, 258, false},
        {4096, 4, 256, 258, false},
        {8192, 3, 128, 258, false},
    };
}

int main() {
    auto corpora = generate_corpora();
    auto params = get_params();

    for (size_t ci = 0; ci < corpora.size(); ++ci) {
        const auto& corpus = corpora[ci];
        for (size_t pi = 0; pi < params.size(); ++pi) {
            const auto& p = params[pi];

            DeflateConfig cfg(p.search_size, p.lookahead_max, p.min_match,
                              false, 8, 8, p.use_flag, p.max_chain_length);

            auto result = pipeline::compress_bytes_deflate(corpus.data, cfg);

            fprintf(stderr, "[NEW] corpus=%s search=%zu mm=%zu chain=%zu la=%zu flag=%d\n",
                    corpus.name, p.search_size, p.min_match,
                    p.max_chain_length, p.lookahead_max, (int)p.use_flag);

            fprintf(stderr, "[NEW] triples=%zu compressed=%zu\n",
                    result.triples.size(), result.compressed.size());

            printf("===CORPUS_START===\n");
            printf("NAME %s\n", corpus.name);
            printf("SEARCH %zu\n", p.search_size);
            printf("MM %zu\n", p.min_match);
            printf("CHAIN %zu\n", p.max_chain_length);
            printf("LA %zu\n", p.lookahead_max);
            printf("FLAG %d\n", (int)p.use_flag);
            printf("TRIPLES %zu\n", result.triples.size());
            printf("COMPRESSED %zu\n", result.compressed.size());

            printf("TRIPLES_DATA ");
            for (size_t i = 0; i < result.triples.size(); ++i) {
                const auto& t = result.triples[i];
                printf("%u %u %u", t.offset, t.length, t.literal);
                if (i + 1 < result.triples.size()) printf(" ");
            }
            printf("\n");

            printf("COMPRESSED_DATA ");
            for (size_t i = 0; i < result.compressed.size(); ++i) {
                printf("%02x", result.compressed[i]);
            }
            printf("\n");

            printf("===CORPUS_END===\n");
        }
    }

    fprintf(stderr, "[NEW] Done.\n");
    return 0;
}
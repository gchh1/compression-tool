/**
 * @file test_deflate_step_old.cpp
 * @brief 只链接 algorithm (old)，输出旧版 Deflate 的 LZ tokens + 压缩字节流
 */
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <span>
#include <string>
#include <vector>

#include "Deflate.hpp"

using namespace compressor::algorithm;

struct Triple {
    uint32_t offset;
    uint32_t length;
    uint8_t literal;
};

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

            fprintf(stderr, "[OLD] corpus=%s search=%zu mm=%zu chain=%zu la=%zu flag=%d\n",
                    corpus.name, p.search_size, p.min_match,
                    p.max_chain_length, p.lookahead_max, (int)p.use_flag);

            Deflate deflate(p.search_size, p.min_match, p.max_chain_length,
                            p.lookahead_max, p.use_flag);
            deflate.reset();

            std::vector<uint8_t> output;
            output.resize(std::max(corpus.data.size() * 2 + 65536, size_t{4096}));

            auto in_span = std::span<const uint8_t>(corpus.data.data(), corpus.data.size());
            auto out_span = std::span<uint8_t>(output.data(), output.size());

            auto st1 = deflate.process(in_span, out_span, false);

            const auto& tokens = deflate.tokens();
            fprintf(stderr, "[OLD] after first pass: consumed=%zu tokens=%zu state=%d done=%d\n",
                    st1.bytes_consumed, tokens.size(),
                    (int)deflate.state(), (int)st1.done);

            std::vector<Triple> old_triples;
            old_triples.reserve(tokens.size());
            for (const auto& t : tokens) {
                if (t.is_literal) {
                    old_triples.push_back({0, 1, static_cast<uint8_t>(t.code)});
                } else {
                    old_triples.push_back({t.match_dist, t.match_len, 0});
                }
            }

            std::span<uint8_t> out2(output.data() + st1.bytes_produced,
                                     output.size() - st1.bytes_produced);
            auto st2 = deflate.process(std::span<const uint8_t>{}, out2, true);
            size_t total_out = st1.bytes_produced + st2.bytes_produced;
            output.resize(total_out);

            bool is_closed = (total_out >= 2 &&
                              output[total_out - 2] == 0x4E &&
                              output[total_out - 1] == 0xFF);
            if (is_closed) {
                size_t trimmed = total_out;
                while (trimmed >= 2 && output[trimmed - 2] == 0x4E && output[trimmed - 1] == 0xFF) {
                    trimmed -= 2;
                }
                output.resize(trimmed);
            }

            fprintf(stderr, "[OLD] final: triples=%zu compressed=%zu\n",
                    old_triples.size(), output.size());

            printf("===CORPUS_START===\n");
            printf("NAME %s\n", corpus.name);
            printf("SEARCH %zu\n", p.search_size);
            printf("MM %zu\n", p.min_match);
            printf("CHAIN %zu\n", p.max_chain_length);
            printf("LA %zu\n", p.lookahead_max);
            printf("FLAG %d\n", (int)p.use_flag);
            printf("TRIPLES %zu\n", old_triples.size());
            printf("COMPRESSED %zu\n", output.size());

            printf("TRIPLES_DATA ");
            for (size_t i = 0; i < old_triples.size(); ++i) {
                printf("%u %u %u", old_triples[i].offset, old_triples[i].length, old_triples[i].literal);
                if (i + 1 < old_triples.size()) printf(" ");
            }
            printf("\n");

            printf("COMPRESSED_DATA ");
            for (size_t i = 0; i < output.size(); ++i) {
                printf("%02x", output[i]);
            }
            printf("\n");

            printf("===CORPUS_END===\n");
        }
    }

    fprintf(stderr, "[OLD] Done.\n");
    return 0;
}
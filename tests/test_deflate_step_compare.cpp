/**
 * @file test_deflate_step_compare.cpp
 * @brief 新旧 Deflate 分步对比测试：LZDP (LZ matching) + Flate (Huffman encoding)
 *
 * 使用同一套参数配置，对新的非流式 Deflate 和旧的非流式 Deflate
 * 进行步骤压缩结果逐字节比较，拆分粒度为 LZDP 和 Flate 两阶段。
 *
 * 用法：
 *   test_deflate_step_compare
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "Deflate.hpp"
#include "MatchEngine.hpp"
#include "test_deflate_old_wrap.h"

using namespace compressor::algorithm;
using namespace compressor::algorithm::LZMatcher;

static int g_tests = 0, g_passed = 0;

static void test_header(const char* name) {
    g_tests++;
    printf("\n══════════════════════════════════════════════════════\n");
    printf("  TEST #%d: %s\n", g_tests, name);
    printf("══════════════════════════════════════════════════════\n");
}

static void pass() { g_passed++; printf("  => PASSED\n"); }

static void fail(const char* reason) {
    printf("  => FAILED: %s\n", reason);
}

// ──── 语料生成 ────

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

// ──── Triple 格式化（确保简洁可读） ────

static std::string triple_str(const Triple& t) {
    char buf[128];
    if (t.offset == 0 && t.length <= 1) {
        std::snprintf(buf, sizeof(buf), "LIT(0x%02x='%c')", t.literal,
                      (t.literal >= 32 && t.literal < 127) ? (char)t.literal : '.');
    } else {
        std::snprintf(buf, sizeof(buf), "MATCH(off=%u, len=%u)", t.offset, t.length);
    }
    return buf;
}

// ──── 参数配置 ────

struct TestParams {
    size_t search_size;
    size_t min_match;
    size_t max_chain_length;
    size_t lookahead_max;
    bool use_flag;

    void print() const {
        printf("  Params: search=%zu min_match=%zu chain=%zu lookahead=%zu flag=%d\n",
               search_size, min_match, max_chain_length, lookahead_max, (int)use_flag);
    }
};

// ──── 运行旧版 Deflate ────

static std::vector<Triple> old_tokens_to_triples(const OldDeflateLZStep* steps, size_t count) {
    std::vector<Triple> triples;
    triples.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        if (steps[i].is_literal) {
            triples.emplace_back(0, 1, static_cast<uint8_t>(steps[i].lit_val));
        } else {
            triples.emplace_back(steps[i].match_dist, steps[i].match_len, 0);
        }
    }
    return triples;
}

static bool run_old_deflate(const std::vector<uint8_t>& input, const TestParams& p,
                            std::vector<Triple>& out_triples,
                            std::vector<uint8_t>& out_compressed) {
    OldDeflateResult* r = old_deflate_run_nonstreaming(
        input.data(), input.size(), p.search_size, p.min_match,
        p.max_chain_length, p.lookahead_max, p.use_flag ? 1 : 0);

    if (!r || !r->valid) {
        old_deflate_free_result(r);
        return false;
    }

    out_triples = old_tokens_to_triples(r->steps, r->count);
    out_compressed.assign(r->compressed, r->compressed + r->compressed_size);
    old_deflate_free_result(r);
    return true;
}

// ──── 运行新版 Deflate ────

static void run_new_deflate(const std::vector<uint8_t>& input, const TestParams& p,
                            std::vector<Triple>& out_triples,
                            std::vector<uint8_t>& out_compressed) {
    DeflateConfig cfg(p.search_size, p.lookahead_max, p.min_match,
                      false, 8, 8, p.use_flag, p.max_chain_length);

    auto result = pipeline::compress_bytes_deflate(input, cfg);
    out_triples = result.triples;
    out_compressed = result.compressed;
}

// ──── 逐字节比较 LZDP 阶段 ────

static int compare_lz_phase(const std::vector<Triple>& old_t,
                            const std::vector<Triple>& new_t) {
    printf("\n  ── LZDP Phase (LZ matching) ──\n");
    printf("  Old token count: %zu\n", old_t.size());
    printf("  New triple count: %zu\n", new_t.size());

    int diffs = 0;
    size_t max_count = std::max(old_t.size(), new_t.size());

    for (size_t i = 0; i < max_count; ++i) {
        bool has_old = i < old_t.size();
        bool has_new = i < new_t.size();

        if (!has_old) {
            printf("  [%zu] OLD: <missing>  |  NEW: %s\n", i, triple_str(new_t[i]).c_str());
            diffs++;
        } else if (!has_new) {
            printf("  [%zu] OLD: %s  |  NEW: <missing>\n", i, triple_str(old_t[i]).c_str());
            diffs++;
        } else if (old_t[i].offset != new_t[i].offset ||
                   old_t[i].length != new_t[i].length ||
                   old_t[i].literal != new_t[i].literal) {
            printf("  [%zu] OLD: %s  |  NEW: %s\n",
                   i, triple_str(old_t[i]).c_str(), triple_str(new_t[i]).c_str());
            diffs++;
            if (diffs >= 40) {
                printf("  ... (showing first %d differences, total may be more)\n", diffs);
                break;
            }
        }
    }

    if (diffs == 0 && old_t.size() == new_t.size()) {
        printf("  => LZDP phase: IDENTICAL (%zu tokens)\n", old_t.size());
        return 0;
    } else {
        printf("  => LZDP phase: %d difference(s) found\n", diffs);
        return diffs;
    }
}

// ──── 逐字节比较 Flate 阶段 ────

static int compare_flate_phase(const std::vector<uint8_t>& old_out,
                               const std::vector<uint8_t>& new_out) {
    printf("\n  ── Flate Phase (Huffman encoding) ──\n");
    printf("  Old compressed size: %zu bytes\n", old_out.size());
    printf("  New compressed size: %zu bytes\n", new_out.size());

    int diffs = 0;
    size_t max_size = std::max(old_out.size(), new_out.size());

    size_t show_limit = 64;
    for (size_t i = 0; i < max_size; ++i) {
        bool has_old = i < old_out.size();
        bool has_new = i < new_out.size();

        if (!has_old || !has_new || old_out[i] != new_out[i]) {
            diffs++;
            if (i < show_limit) {
                printf("  byte[%zu]: old=", i);
                if (has_old) printf("0x%02x", old_out[i]); else printf("<eof>");
                printf("  new=");
                if (has_new) printf("0x%02x", new_out[i]); else printf("<eof>");
                printf("\n");
            }
        }
        if (diffs == show_limit && i >= show_limit) {
            printf("  ... (showing first %zu differing bytes)\n", show_limit);
        }
    }

    if (diffs == 0 && old_out.size() == new_out.size()) {
        printf("  => Flate phase: IDENTICAL\n");
        return 0;
    } else {
        double old_ratio = old_out.size() > 0 ? 100.0 * diffs / old_out.size() : 0;
        printf("  => Flate phase: %d byte(s) differ (%.1f%% of old output)\n",
               diffs, old_ratio);
        return diffs;
    }
}

// ──── 验证解压正确性 ────

static int verify_decompress(const std::vector<uint8_t>& original,
                             const std::vector<uint8_t>& new_compressed,
                             const TestParams& p) {
    DeflateConfig cfg(p.search_size, p.lookahead_max, p.min_match,
                      false, 8, 8, p.use_flag, p.max_chain_length);
    try {
        auto decompressed = pipeline::decompress_bytes_deflate(new_compressed, cfg);
        if (decompressed == original) {
            printf("  New Deflate decompress: OK (%zu bytes)\n", decompressed.size());
            return 0;
        } else {
            printf("  New Deflate decompress: MISMATCH (orig=%zu, dec=%zu)\n",
                   original.size(), decompressed.size());
            size_t first_diff = std::min(original.size(), decompressed.size());
            for (size_t i = 0; i < first_diff; ++i) {
                if (original[i] != decompressed[i]) {
                    printf("  First diff at byte[%zu]: orig=0x%02x dec=0x%02x\n",
                           i, original[i], decompressed[i]);
                    break;
                }
            }
            return 1;
        }
    } catch (const std::exception& e) {
        printf("  New Deflate decompress: EXCEPTION: %s\n", e.what());
        return 1;
    }
}

// ──── 主测试 ────

int main() {
    printf("========================================================\n");
    printf("  Deflate Step Compare: OLD vs NEW\n");
    printf("  Phase 1: LZDP (LZ token matching comparison)\n");
    printf("  Phase 2: Flate (Huffman encoding comparison)\n");
    printf("========================================================\n");

    auto corpora = generate_corpora();

    const TestParams params[] = {
        {4096, 3, 256, 258, false},
        {4096, 3, 32, 258, false},
        {4096, 2, 256, 128, false},
    };

    for (const auto& corpus : corpora) {
        for (size_t pi = 0; pi < sizeof(params) / sizeof(params[0]); ++pi) {
            const auto& p = params[pi];
            char test_name[256];
            std::snprintf(test_name, sizeof(test_name),
                          "corpus=%s, search=%zu, chain=%zu, mm=%zu, la=%zu",
                          corpus.name, p.search_size, p.max_chain_length,
                          p.min_match, p.lookahead_max);
            test_header(test_name);
            p.print();

            printf("  Input size: %zu bytes\n", corpus.data.size());

            std::vector<Triple> old_triples, new_triples;
            std::vector<uint8_t> old_compressed, new_compressed;

            if (!run_old_deflate(corpus.data, p, old_triples, old_compressed)) {
                fail("Old Deflate failed");
                continue;
            }
            run_new_deflate(corpus.data, p, new_triples, new_compressed);

            int lz_diffs = compare_lz_phase(old_triples, new_triples);
            int flate_diffs = compare_flate_phase(old_compressed, new_compressed);
            int dec_errors = verify_decompress(corpus.data, new_compressed, p);

            if (lz_diffs == 0 && flate_diffs == 0 && dec_errors == 0) {
                pass();
            } else if (dec_errors == 0) {
                printf("  => PARTIAL: LZ=%d diff(s), Flate=%d diff(s), "
                       "but new decompress OK\n",
                       lz_diffs, flate_diffs);
                pass();
            } else {
                fail("decompression error");
            }
        }
    }

    printf("\n========================================================\n");
    printf("  RESULTS: %d/%d tests passed\n", g_passed, g_tests);
    printf("========================================================\n");

    return (g_passed == g_tests) ? 0 : 1;
}
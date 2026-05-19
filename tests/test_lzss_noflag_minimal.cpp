#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

#include "LZSS.hpp"
#include "LZencoding.hpp"
#include "EncodingTriple.hpp"
#include "BitProcessor.hpp"

int main() {
    using namespace compressor::algorithm;

    const std::string para =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
        "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. "
        "Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris "
        "nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in "
        "reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla "
        "pariatur. Excepteur sint occaecat cupidatat non proident, sunt in "
        "culpa qui officia deserunt mollit anim id est laborum. ";
    std::vector<uint8_t> input;
    const size_t len = 200;
    input.reserve(len);
    while (input.size() < len) {
        for (char c : para) {
            if (input.size() >= len) break;
            input.push_back(static_cast<uint8_t>(c));
        }
    }

    LZSSConfig cfg(4095, 255, 3, false);
    LZSS lzss(cfg);
    auto triples = lzss.greedyMatch(input);

    auto run_triples = literalrun(triples, cfg.window.look_size);

    // Find the run_triples entries that correspond to greedyMatch[120-130]
    // First compute the mapping
    printf("GreedyMatch entries around 120-130:\n");
    for (size_t i = 118; i < std::min(triples.size(), (size_t)132); ++i) {
        auto& t = triples[i];
        const char* type = (t.offset > 0) ? "MATCH" : "LIT";
        printf("  gm[%zu] (%u,%u,%u) %s", i, t.offset, t.length, t.literal, type);
        if (t.offset == 0 && t.length == 1) printf(" '%c'", (char)t.literal);
        printf("\n");
    }

    // Now find where these appear in run_triples
    printf("\nRun_triples entries (full):\n");
    size_t gm_idx = 0;
    for (size_t ri = 0; ri < run_triples.size() && gm_idx < triples.size(); ++ri) {
        auto& rt = run_triples[ri];
        if (rt.offset == 0 && rt.length > 1) {
            printf("  rt[%zu] RUN(%u)\n", ri, rt.length);
            gm_idx += rt.length;  // Skip the literals in the run
        } else if (rt.offset > 0) {
            printf("  rt[%zu] MATCH(%u,%u)\n", ri, rt.offset, rt.length);
            gm_idx += 1;
        } else {
            printf("  rt[%zu] LIT(%u) ", ri, rt.literal);
            if (rt.literal >= 32 && rt.literal < 127) printf("'%c'", (char)rt.literal);
            printf("\n");
            gm_idx += 1;
        }
    }

    return 0;
}
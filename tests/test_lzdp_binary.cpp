#include <cstdio>
#include <cstdint>
#include <vector>
#include <random>
#include "LZDPCompressor.hpp"

int main() {
    for (size_t sz : {1024, 4096, 16384, 32768, 65536}) {
        fprintf(stderr, "[TEST] Testing LZDP with %zu bytes...\n", sz);
        fflush(stderr);

        std::vector<uint8_t> data(sz);
        std::mt19937 gen(67890);
        for (auto& b : data) b = static_cast<uint8_t>(gen() & 0xFF);

        compressor::core::LZDPCompressor comp;
        comp.set_search_size(4096);
        comp.set_lookahead_size(256);
        comp.set_min_match(0);
        comp.set_dp_top(3);
        comp.set_use_flag_encoding(false);
        comp.set_match_engine(0);

        auto cr = comp.compress(data);
        fprintf(stderr, "[TEST]   Compressed: %zu -> %zu bytes\n", sz, cr.data.size());
        fflush(stderr);

        auto dec = comp.decompress(cr.data);
        if (dec.data.size() != data.size() || dec.data != data) {
            fprintf(stderr, "[TEST]   FAIL!\n");
            return 1;
        }
        fprintf(stderr, "[TEST]   PASS\n");
        fflush(stderr);
    }
    fprintf(stderr, "[TEST] All sizes PASS\n");
    return 0;
}
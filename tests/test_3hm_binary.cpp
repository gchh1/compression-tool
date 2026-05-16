#include <cstdio>
#include <cstdint>
#include <vector>
#include <random>
#include "DPFlateCompressor.hpp"

int main() {
    fprintf(stderr, "[TEST] Creating 64k random data...\n");
    fflush(stderr);

    std::vector<uint8_t> data(65536);
    std::mt19937 gen(67890);
    for (auto& b : data) b = static_cast<uint8_t>(gen() & 0xFF);

    fprintf(stderr, "[TEST] Data created, size=%zu\n", data.size());
    fflush(stderr);

    fprintf(stderr, "[TEST] Compressing with 3HfMT...\n");
    fflush(stderr);

    compressor::core::DPFlateCompressor comp;
    comp.set_search_size(4096);
    comp.set_lookahead_size(256);
    comp.set_min_match(4);
    comp.set_max_chain_length(256);
    comp.set_dp_sub_match_max(6);
    comp.set_match_engine(1);
    comp.set_use_flag_encoding(false);
    comp.set_use_3hfmtree(true);
    comp.set_huffman_chunk_bits(8);

    auto cr = comp.compress(data);
    fprintf(stderr, "[TEST] Compressed: %zu -> %zu bytes, ratio=%.2f%%\n",
            data.size(), cr.data.size(), cr.compression_ratio);
    fflush(stderr);

    fprintf(stderr, "[TEST] Decompressing...\n");
    fflush(stderr);

    auto dec = comp.decompress(cr.data);
    fprintf(stderr, "[TEST] Decompressed: %zu bytes\n", dec.data.size());
    fflush(stderr);

    if (dec.data.size() != data.size()) {
        fprintf(stderr, "[TEST] FAIL: size mismatch %zu vs %zu\n", data.size(), dec.data.size());
        return 1;
    }

    if (dec.data != data) {
        fprintf(stderr, "[TEST] FAIL: content mismatch\n");
        for (size_t i = 0; i < data.size(); ++i) {
            if (data[i] != dec.data[i]) {
                fprintf(stderr, "[TEST] First diff at byte %zu: orig=0x%02x dec=0x%02x\n",
                        i, data[i], dec.data[i]);
                break;
            }
        }
        return 1;
    }

    fprintf(stderr, "[TEST] PASS: 3HfMT binary_64k roundtrip OK\n");
    return 0;
}
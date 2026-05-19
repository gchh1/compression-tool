#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Deflate.hpp"

static bool check_roundtrip(const std::vector<uint8_t>& input,
                            const compressor::algorithm::DeflateConfig& cfg,
                            const char* label) {
    using namespace compressor::algorithm;
    Deflate deflate(cfg);
    auto triples = deflate.hashChainMatch(input);

    Huffman_InflateConfig icfg{
        cfg.encoding.offset_bits,
        cfg.encoding.length_bits,
        cfg.huffman.huffman_offset_bitwidth,
        cfg.huffman.huffman_length_bitwidth
    };
    Huffman_Inflate encoder(icfg, cfg.encoding);
    encoder.countFreq(triples);
    encoder.buildTree();
    auto encoded = encoder.encode(triples);
    auto decoded = encoder.decode(encoded);

    bool ok = (decoded.size() == triples.size());
    if (ok) {
        for (size_t i = 0; i < triples.size(); ++i) {
            if (triples[i].offset != decoded[i].offset ||
                triples[i].length != decoded[i].length ||
                triples[i].literal != decoded[i].literal) {
                ok = false;
                printf("  [%s] MISMATCH at %zu: orig(%u,%u,%u) vs dec(%u,%u,%u)\n",
                       label, i,
                       triples[i].offset, triples[i].length, (unsigned)triples[i].literal,
                       decoded[i].offset, decoded[i].length, (unsigned)decoded[i].literal);
                break;
            }
        }
    }

    compressor::utils::_buffer pending;
    auto output = decode_triple(decoded, cfg.encoding, pending);
    bool data_ok = (output == input);

    printf("[%s] triples=%zu encoded=%zu decoded=%zu triple_match=%s data_match=%s\n",
           label, triples.size(), encoded.size(), decoded.size(),
           ok ? "OK" : "FAIL", data_ok ? "OK" : "FAIL");

    if (!data_ok) {
        printf("  expected %zu bytes, got %zu bytes\n", input.size(), output.size());
        for (size_t i = 0; i < output.size() && i < 20; ++i) {
            if (i < input.size() && input[i] != output[i]) {
                printf("  diff at %zu: expected 0x%02x got 0x%02x\n", i, input[i], output[i]);
            }
        }
    }

    return ok && data_ok;
}

int main() {
    using namespace compressor::algorithm;
    int pass = 0, fail = 0;

    {
        std::vector<uint8_t> input = { 'h','e','l','l','o' };
        DeflateConfig cfg(512, 128, 3, false, 8, 8, true);
        if (check_roundtrip(input, cfg, "hello_noflag")) ++pass; else ++fail;
    }

    {
        std::vector<uint8_t> input = { 'h','e','l','l','o' };
        DeflateConfig cfg(512, 128, 3, false, 8, 8, true);
        if (check_roundtrip(input, cfg, "hello_flag")) ++pass; else ++fail;
    }

    {
        std::vector<uint8_t> input;
        for (int i = 0; i < 1000; ++i) input.push_back(static_cast<uint8_t>("abcabcabcabcabcabcabc"[i % 21]));
        DeflateConfig cfg(512, 128, 3, false, 8, 8, true);
        if (check_roundtrip(input, cfg, "repeat_noflag")) ++pass; else ++fail;
    }

    {
        std::vector<uint8_t> input;
        for (int i = 0; i < 1000; ++i) input.push_back(static_cast<uint8_t>(i & 0xFF));
        DeflateConfig cfg(512, 128, 3, false, 8, 8, true);
        if (check_roundtrip(input, cfg, "sequential_noflag")) ++pass; else ++fail;
    }

    printf("\n=== Huffman_Inflate roundtrip: %d pass, %d fail ===\n", pass, fail);
    return fail;
}

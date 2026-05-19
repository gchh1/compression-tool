#include <cassert>
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

    const std::string base = "REGRESS_LZDP_DPFLATE_";
    std::vector<uint8_t> input;
    const size_t len = 672;
    input.reserve(len);
    while (input.size() < len) {
        for (char c : base) {
            if (input.size() >= len) break;
            input.push_back(static_cast<uint8_t>(c));
        }
    }

    LZSSConfig cfg(4095, 255, 3, false);
    LZSS lzss(cfg);
    auto triples = lzss.greedyMatch(input);
    auto run_triples = literalrun(triples, cfg.window.look_size);

    compressor::utils::_buffer pending;
    auto encoded = encoding_triple_lz(run_triples, cfg.encoding, pending, true);
    std::cout << "Encoded: " << encoded.size() << " bytes\n";

    compressor::utils::_buffer pending_r;
    auto decoded = readtriple(encoded, cfg.encoding, pending_r);
    std::cout << "readtriple returned: " << decoded.size() << " triples\n";

    compressor::utils::_buffer pending_d;
    auto result = decode_triple(decoded, cfg.encoding, pending_d);
    std::cout << "decode_triple: " << result.size() << " bytes\n";

    if (result == input) {
        std::cout << "ROUNDTRIP OK!\n";
    } else {
        std::cout << "ROUNDTRIP FAILED!\n";
        return 1;
    }
    return 0;
}
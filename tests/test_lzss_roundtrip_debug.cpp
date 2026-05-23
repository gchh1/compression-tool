#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include "LZSS.hpp"
#include "LZencoding.hpp"
#include "LZencoding.hpp"
#include "BitProcessor.hpp"

int main() {
    using namespace compressor::algorithm;

    const std::string text = "REGRESS_LZDP_DPFLATE_";
    std::vector<uint8_t> input(text.begin(), text.end());

    LZSSConfig cfg(4095, 255, 3, true);
    LZSS lzss(cfg);

    auto triples = lzss.greedyMatch(input);

    std::cout << "Triples count: " << triples.size() << "\n";
    for (size_t i = 0; i < triples.size(); ++i) {
        std::cout << "  [" << i << "] off=" << triples[i].offset
                  << " len=" << triples[i].length
                  << " lit=" << (int)triples[i].literal << "\n";
    }

    compressor::utils::_buffer pending;
    auto encoded = encoding_triple_lz(triples, cfg.encoding, pending, true);
    std::cout << "Encoded bytes: " << encoded.size() << "\n";

    compressor::utils::_buffer pending2;
    auto decoded_triples = readtriple(encoded, cfg.encoding, pending2);
    std::cout << "Decoded triples count: " << decoded_triples.size() << "\n";
    for (size_t i = 0; i < std::min(decoded_triples.size(), size_t{20}); ++i) {
        std::cout << "  [" << i << "] off=" << decoded_triples[i].offset
                  << " len=" << decoded_triples[i].length
                  << " lit=" << (int)decoded_triples[i].literal << "\n";
    }

    assert(triples.size() == decoded_triples.size());
    for (size_t i = 0; i < triples.size(); ++i) {
        assert(triples[i].offset == decoded_triples[i].offset);
        assert(triples[i].length == decoded_triples[i].length);
        assert(triples[i].literal == decoded_triples[i].literal);
    }
    std::cout << "readtriple roundtrip OK\n";

    compressor::utils::_buffer pending3;
    auto result = decode_triple(decoded_triples, cfg.encoding, pending3);
    std::cout << "Result bytes: " << result.size() << " input bytes: " << input.size() << "\n";

    assert(result == input);
    std::cout << "decode_triple roundtrip OK\n";

    return 0;
}
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

#include "LZSS.hpp"
#include "LZencoding.hpp"
#include "LZencoding.hpp"
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
    const size_t len = 10000;
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

    // Test 1: Decode directly from greedyMatch triples (without literalrun encoding)
    compressor::utils::_buffer pending_d1;
    auto direct = decode_triple(triples, cfg.encoding, pending_d1);
    std::cout << "Direct decode (greedyMatch): " << direct.size() << " bytes"
              << " (expected " << input.size() << ")\n";
    if (direct.size() == input.size() && memcmp(direct.data(), input.data(), input.size()) == 0) {
        std::cout << "Direct decode OK!\n";
    } else {
        std::cout << "Direct decode FAILED!\n";
        size_t first_diff = 0;
        for (size_t i = 0; i < std::min(direct.size(), input.size()); ++i) {
            if (direct[i] != input[i]) { first_diff = i; break; }
        }
        std::cout << "  First diff at: " << first_diff << "\n";
    }

    // Test 2: Full encode/decode roundtrip
    auto run_triples = literalrun(triples, cfg.window.look_size);
    compressor::utils::_buffer pending_e;
    auto encoded = encoding_triple_lz(run_triples, cfg.encoding, pending_e, true);

    compressor::utils::_buffer pending_r;
    auto decoded = readtriple(encoded, cfg.encoding, pending_r);
    compressor::utils::_buffer pending_d2;
    auto result = decode_triple(decoded, cfg.encoding, pending_d2);
    std::cout << "\nRoundtrip decode: " << result.size() << " bytes (expected " << input.size() << ")\n";
    if (result.size() == input.size() && memcmp(result.data(), input.data(), input.size()) == 0) {
        std::cout << "Roundtrip OK!\n";
    } else {
        std::cout << "Roundtrip FAILED!\n";
        size_t first_diff = 0;
        for (size_t i = 0; i < std::min(result.size(), input.size()); ++i) {
            if (result[i] != input[i]) { first_diff = i; break; }
        }
        std::cout << "  First diff at: " << first_diff << "\n";
    }

    // Count triple types
    size_t lit_count = 0, match_count = 0, run_count = 0;
    for (auto& t : run_triples) {
        if (t.offset == 0 && t.length == 1) lit_count++;
        else if (t.offset == 0 && t.length > 1) run_count++;
        else match_count++;
    }
    std::cout << "\nrun_triples: " << run_triples.size() << " total ("
              << lit_count << " lits, " << match_count << " matches, "
              << run_count << " runs)\n";

    lit_count = match_count = run_count = 0;
    for (auto& t : decoded) {
        if (t.offset == 0 && t.length == 1) lit_count++;
        else if (t.offset == 0 && t.length > 1) run_count++;
        else match_count++;
    }
    std::cout << "decoded: " << decoded.size() << " total ("
              << lit_count << " lits, " << match_count << " matches, "
              << run_count << " runs)\n";

    return 0;
}
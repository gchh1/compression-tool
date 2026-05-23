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

    for (int test_len : {200, 500, 1000, 2000, 5000}) {
        std::vector<uint8_t> input;
        input.reserve(test_len);
        while (input.size() < (size_t)test_len) {
            for (char c : para) {
                if (input.size() >= (size_t)test_len) break;
                input.push_back(static_cast<uint8_t>(c));
            }
        }

        LZSSConfig cfg(4095, 255, 3, false);
        LZSS lzss(cfg);
        auto triples = lzss.greedyMatch(input);
        auto run_triples = literalrun(triples, cfg.window.look_size);

        compressor::utils::_buffer pen;
        auto encoded = encoding_triple_lz(run_triples, cfg.encoding, pen, true);

        compressor::utils::_buffer pdr;
        auto decoded = readtriple(encoded, cfg.encoding, pdr);

        compressor::utils::_buffer pd2;
        auto result = decode_triple(decoded, cfg.encoding, pd2);

        bool ok = (result.size() == input.size() &&
                   memcmp(result.data(), input.data(), input.size()) == 0);
        printf("len=%5d: encoded=%4zuB, triples %zu->%zu, result=%zuB %s\n",
               test_len, encoded.size(), run_triples.size(), decoded.size(),
               result.size(), ok ? "OK" : "FAIL");
    }

    return 0;
}
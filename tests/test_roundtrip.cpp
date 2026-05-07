#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>

#include "Deflate.hpp"
#include "Inflate.hpp"
#include "MyFlate.hpp"

using namespace compressor::algorithm;

bool roundtrip_test(IAlgorithm& compressor, IAlgorithm& decompressor,
                    const std::vector<uint8_t>& original,
                    const std::string& name) {
    size_t out_cap = original.size() * 2 + 65536;
    std::vector<uint8_t> compressed(out_cap);

    auto cs = compressor.process(original, compressed, true);
    compressed.resize(cs.bytes_produced);

    std::cout << "[" << name << "] original=" << original.size()
              << " compressed=" << compressed.size()
              << " ratio=" << (float)compressed.size() / original.size() * 100 << "%" << std::endl;

    std::vector<uint8_t> decompressed(original.size() + 65536);
    auto ds = decompressor.process(compressed, decompressed, true);
    decompressed.resize(ds.bytes_produced);

    std::cout << "[" << name << "] decompressed=" << decompressed.size()
              << " done=" << ds.done << std::endl;

    if (decompressed.size() != original.size()) {
        std::cerr << "[" << name << "] SIZE MISMATCH: " << decompressed.size()
                  << " vs " << original.size() << std::endl;
        return false;
    }

    for (size_t i = 0; i < original.size(); i++) {
        if (decompressed[i] != original[i]) {
            std::cerr << "[" << name << "] DATA MISMATCH at byte " << i
                      << ": got " << (int)decompressed[i]
                      << " expected " << (int)original[i] << std::endl;
            return false;
        }
    }

    std::cout << "[" << name << "] PASS" << std::endl;
    return true;
}

int main() {
    bool all_pass = true;

    std::string pattern = "HELLO_DEFLATE_WORLD_";
    std::vector<uint8_t> text_data;
    for (int i = 0; i < 500; i++) {
        text_data.insert(text_data.end(), pattern.begin(), pattern.end());
    }

    std::vector<uint8_t> random_data(10000);
    std::mt19937 rng(42);
    for (auto& b : random_data) b = rng() & 0xFF;

    {
        Deflate def;
        Inflate inf;
        all_pass &= roundtrip_test(def, inf, text_data, "Deflate/text");
    }

    {
        Deflate def;
        Inflate inf;
        all_pass &= roundtrip_test(def, inf, random_data, "Deflate/random");
    }

    {
        MyFlate myf;
        Inflate inf;
        all_pass &= roundtrip_test(myf, inf, text_data, "MyFlate/text");
    }

    {
        MyFlate myf;
        Inflate inf;
        all_pass &= roundtrip_test(myf, inf, random_data, "MyFlate/random");
    }

    std::cout << "\n" << (all_pass ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << std::endl;
    return all_pass ? 0 : 1;
}

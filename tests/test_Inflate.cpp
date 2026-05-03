#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "api.hpp"

using namespace compressor;

int main() {
    std::cout << "=== Inflate Round-Trip Tests ===" << std::endl;

    // Test 1: Simple string
    {
        std::string input = "Hello World! This is a Deflate round-trip test.";
        std::vector<uint8_t> original(input.begin(), input.end());

        auto comp = api::compress(original, std::array{api::AlgorithmID::Deflate});
        assert(comp.success);
        std::cout << "Test 1 (string): " << original.size() << "B -> "
                  << comp.compressed_size << "B ("
                  << (100.0 * comp.compressed_size / original.size()) << "%)"
                  << std::endl;

        auto decomp = api::decompress(comp.data, std::array{api::AlgorithmID::Inflate});
        assert(decomp.success);
        assert(decomp.data == original);
        std::cout << "  [PASS] Round-trip verified" << std::endl;
    }

    // Test 2: Repetitive data (exercises LZSS match copies)
    {
        std::string pattern = "DEFLATE_ROUNDTRIP_TEST_PATTERN_";
        std::vector<uint8_t> original;
        for (int i = 0; i < 500; ++i)
            original.insert(original.end(), pattern.begin(), pattern.end());

        auto comp = api::compress(original, std::array{api::AlgorithmID::Deflate});
        assert(comp.success);
        std::cout << "Test 2 (repetitive): " << original.size() << "B -> "
                  << comp.compressed_size << "B ("
                  << (100.0 * comp.compressed_size / original.size()) << "%)"
                  << std::endl;

        auto decomp = api::decompress(comp.data, std::array{api::AlgorithmID::Inflate});
        assert(decomp.success);
        assert(decomp.data == original);
        std::cout << "  [PASS] Round-trip verified" << std::endl;
    }

    // Test 3: Random-like data (exercises all code paths)
    {
        std::vector<uint8_t> original(10000);
        for (size_t i = 0; i < original.size(); ++i)
            original[i] = static_cast<uint8_t>((i * 7 + 13) & 0xFF);

        auto comp = api::compress(original, std::array{api::AlgorithmID::Deflate});
        assert(comp.success);
        std::cout << "Test 3 (pseudo-random): " << original.size() << "B -> "
                  << comp.compressed_size << "B ("
                  << (100.0 * comp.compressed_size / original.size()) << "%)"
                  << std::endl;

        auto decomp = api::decompress(comp.data, std::array{api::AlgorithmID::Inflate});
        assert(decomp.success);
        assert(decomp.data == original);
        std::cout << "  [PASS] Round-trip verified" << std::endl;
    }

    // Test 4: Large data to trigger multiple blocks
    {
        std::vector<uint8_t> original(200000);
        for (size_t i = 0; i < original.size(); ++i)
            original[i] = static_cast<uint8_t>(i * 3 + 7);

        auto comp = api::compress(original, std::array{api::AlgorithmID::Deflate});
        assert(comp.success);
        std::cout << "Test 4 (multi-block): " << original.size() << "B -> "
                  << comp.compressed_size << "B ("
                  << (100.0 * comp.compressed_size / original.size()) << "%)"
                  << std::endl;

        auto decomp = api::decompress(comp.data, std::array{api::AlgorithmID::Inflate});
        assert(decomp.success);
        if (decomp.data != original) {
            std::cerr << "  MISMATCH: decsz=" << decomp.data.size() << " orsz=" << original.size() << std::endl;
            for (size_t i = 0; i < std::min(original.size(), decomp.data.size()); ++i) {
                if (original[i] != decomp.data[i]) {
                    std::cerr << "  First diff at [" << i << "]: orig=0x"
                              << std::hex << (int)original[i] << " dec=0x"
                              << (int)decomp.data[i] << std::dec << std::endl;
                    std::cerr << "  Context orig: ";
                    for (size_t j = (i>8?i-8:0); j < std::min(i+16, original.size()); ++j)
                        std::cerr << std::hex << (int)original[j] << " ";
                    std::cerr << std::dec << "\n  Context dec:  ";
                    for (size_t j = (i>8?i-8:0); j < std::min(i+16, decomp.data.size()); ++j)
                        std::cerr << std::hex << (int)decomp.data[j] << " ";
                    std::cerr << std::dec << std::endl;
                    break;
                }
            }
            assert(false);
        }
        std::cout << "  [PASS] Round-trip verified" << std::endl;
    }

    // Test 5: Edge case — single byte
    {
        std::vector<uint8_t> original = {'X'};

        auto comp = api::compress(original, std::array{api::AlgorithmID::Deflate});
        assert(comp.success);

        auto decomp = api::decompress(comp.data, std::array{api::AlgorithmID::Inflate});
        assert(decomp.success);
        assert(decomp.data == original);
        std::cout << "Test 5 (single byte): [PASS]" << std::endl;
    }

    // Test 6: Edge case — empty
    {
        std::vector<uint8_t> original;

        auto comp = api::compress(original, std::array{api::AlgorithmID::Deflate});
        assert(comp.success);

        auto decomp = api::decompress(comp.data, std::array{api::AlgorithmID::Inflate});
        assert(decomp.success);
        assert(decomp.data == original);
        std::cout << "Test 6 (empty): [PASS]" << std::endl;
    }

    std::cout << "\n[ALL PASS] Inflate round-trip tests" << std::endl;
    return 0;
}

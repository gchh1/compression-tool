#include <cstdio>
#include <random>
#include <vector>

#include "Deflate.hpp"
#include "Inflate.hpp"

using namespace compressor::algorithm;

int main() {
    // Generate 49000 bytes of random data
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 255);
    std::vector<uint8_t> data(49000);
    for (size_t i = 0; i < 49000; ++i)
        data[i] = static_cast<uint8_t>(dist(rng));

    printf("Input: %zu bytes\n", data.size());

    // Compress with raw Deflate
    Deflate deflater;
    std::vector<uint8_t> compressed(data.size() + 65536);
    auto cstat = deflater.process(data, compressed, true);
    compressed.resize(cstat.bytes_produced);
    printf("Compressed: %zu bytes\n", compressed.size());

    // Decompress with raw Inflate
    Inflate inflater;
    std::vector<uint8_t> decompressed(compressed.size() * 4 + 65536);
    auto dstat = inflater.process(compressed, decompressed, true);
    decompressed.resize(dstat.bytes_produced);
    printf("Decompressed: %zu bytes\n", decompressed.size());

    // Compare
    if (decompressed.size() != data.size()) {
        printf("FAIL: size mismatch %zu vs %zu\n", decompressed.size(), data.size());
        // Show first diff
        size_t n = std::min(data.size(), decompressed.size());
        for (size_t i = 0; i < n; i++) {
            if (data[i] != decompressed[i]) {
                printf("  first diff at byte %zu: orig=%u restored=%u\n",
                       i, (unsigned)data[i], (unsigned)decompressed[i]);
                break;
            }
        }
        return 1;
    }

    if (decompressed == data) {
        printf("PASS\n");
        return 0;
    } else {
        printf("FAIL: data mismatch\n");
        return 1;
    }
}

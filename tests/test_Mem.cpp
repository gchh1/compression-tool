#include <cassert>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <random>
#include <vector>
#include "api.hpp"

using namespace compressor::api;
using namespace std::chrono;

int main() {
    constexpr size_t SIZE = 50 * 1024 * 1024;  // 50MB
    std::vector<uint8_t> data(SIZE);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 255);
    for (size_t i = 0; i < SIZE; ++i)
        data[i] = static_cast<uint8_t>(dist(rng));

    auto t0 = high_resolution_clock::now();
    std::vector<AlgorithmID> chain = {AlgorithmID::Deflate};
    auto result = compress(data, chain);
    auto t1 = high_resolution_clock::now();
    std::cout << "compress: " << SIZE << " -> " << result.compressed_size
              << " (" << duration_cast<milliseconds>(t1 - t0).count() << "ms) "
              << (result.success ? "ok" : "FAIL") << std::endl;
    if (!result.success) return 1;

    auto t2 = high_resolution_clock::now();
    std::vector<AlgorithmID> dchain = {AlgorithmID::Inflate};
    auto result2 = decompress(result.data, dchain);
    auto t3 = high_resolution_clock::now();
    std::cout << "decompress: " << result2.compressed_size
              << " bytes (" << duration_cast<milliseconds>(t3 - t2).count() << "ms) "
              << (result2.success ? "ok" : "FAIL") << std::endl;
    if (!result2.success) return 1;

    bool match = (result2.data == data);
    std::cout << (match ? "PASS" : "FAIL - MISMATCH") << std::endl;
    return match ? 0 : 1;
}

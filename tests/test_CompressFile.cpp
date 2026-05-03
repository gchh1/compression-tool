#include <cassert>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <random>
#include <span>
#include <vector>

#include "api.hpp"

using namespace compressor::api;
using namespace std::chrono;

int test(size_t SIZE, const char* label) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 255);
    std::vector<uint8_t> data(SIZE);
    for (size_t i = 0; i < SIZE; ++i) data[i] = static_cast<uint8_t>(dist(rng));

    std::string in = "/tmp/test_cpp_" + std::to_string(SIZE) + ".bin";
    std::string comp = "/tmp/test_cpp_" + std::to_string(SIZE) + ".comp";
    std::string out = "/tmp/test_cpp_" + std::to_string(SIZE) + ".restored";
    {
        FILE* f = fopen(in.c_str(), "wb");
        fwrite(data.data(), 1, SIZE, f);
        fclose(f);
    }

    auto t0 = high_resolution_clock::now();
    auto result = compressFile(in, comp, {{AlgorithmID::Deflate}});
    auto t1 = high_resolution_clock::now();
    if (!result.success) {
        std::cerr << "[" << label << "] COMPRESS FAILED\n";
        return 1;
    }
    std::cout << "[" << label << "] compress: " << SIZE << " -> "
              << result.compressed_size << " ("
              << duration_cast<milliseconds>(t1 - t0).count() << "ms)"
              << std::endl;

    auto t2 = high_resolution_clock::now();
    auto result2 = decompressFile(comp, out, {{AlgorithmID::Inflate}});
    auto t3 = high_resolution_clock::now();
    if (!result2.success) {
        std::cerr << "[" << label << "] DECOMPRESS FAILED\n";
        return 1;
    }
    std::cout << "[" << label << "] decompress: " << result2.compressed_size
              << " bytes (" << duration_cast<milliseconds>(t3 - t2).count()
              << "ms)" << std::endl;

    std::vector<uint8_t> restored(SIZE);
    {
        FILE* f = fopen(out.c_str(), "rb");
        fread(restored.data(), 1, SIZE, f);
        fclose(f);
    }
    bool ok = (data == restored);
    std::cout << "[" << label << "] " << (ok ? "PASS" : "FAIL") << std::endl;
    std::remove(in.c_str());
    std::remove(comp.c_str());
    std::remove(out.c_str());
    return ok ? 0 : 1;
}

int main() {
    int f = 0;
    f += test(1000 * 1024 * 1024, "1000MB");
    return f > 0 ? 1 : 0;
}

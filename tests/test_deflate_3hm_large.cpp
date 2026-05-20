#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

#include "Deflate.hpp"

static std::vector<uint8_t> read_file(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),
                                std::istreambuf_iterator<char>());
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1]
                                : "d:/AAA_C/compression-tool/resources/imdb-movie-reviews-word2vec-tfidf-bow.ipynb";
    const auto input = read_file(path);
    if (input.empty()) {
        std::cerr << "empty or missing input: " << path << '\n';
        return 2;
    }

    const size_t search = argc > 2 ? static_cast<size_t>(std::stoull(argv[2])) : 32767;
    compressor::algorithm::DeflateConfig cfg(
        search, 255, 0, true, 8, 8, true);

    const auto enc = compressor::algorithm::pipeline::compress_bytes_deflate(input, cfg);
    const auto dec =
        compressor::algorithm::pipeline::decompress_bytes_deflate(enc.compressed, cfg);

    const bool ok = dec.size() == input.size() &&
                    (dec.empty() || dec == input);
    std::cout << "input=" << input.size() << " compressed=" << enc.compressed.size()
              << " decompressed=" << dec.size() << (ok ? " PASS\n" : " FAIL\n");
    return ok ? 0 : 1;
}

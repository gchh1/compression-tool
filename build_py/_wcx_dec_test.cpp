#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>
#include "Deflate.hpp"
int main() {
    std::ifstream in(R"(d:\AAA_C\compression-tool\Package\compressed\imdb-movie-reviews-word2vec-tfidf-bow.ipynb.wcx)", std::ios::binary);
    std::vector<uint8_t> wcx((std::istreambuf_iterator<char>(in)), {});
    if (wcx.size() < 20) return 2;
    uint8_t fnlen = wcx[17];
    size_t hdr = 18 + fnlen;
    std::vector<uint8_t> payload(wcx.begin()+hdr, wcx.end());
    const auto orig = []{
        std::ifstream f(R"(d:\AAA_C\compression-tool\resources\imdb-movie-reviews-word2vec-tfidf-bow.ipynb)", std::ios::binary);
        return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
    }();
    compressor::algorithm::DeflateConfig cfg(32767, 255, 0, true, 8, 8, true);
    cfg.encoding.use_flag_encoding = false;
    auto dec = compressor::algorithm::pipeline::decompress_bytes_deflate(payload, cfg);
    bool ok = dec.size()==orig.size() && dec==orig;
    std::cout << "payload="<<payload.size()<<" dec="<<dec.size()<<" expect="<<orig.size()<<(ok?" PASS\n":" FAIL\n");
    return ok?0:1;
}

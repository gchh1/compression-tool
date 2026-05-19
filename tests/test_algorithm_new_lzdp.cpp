#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "LZDPcompressor.hpp"

static std::vector<uint8_t> sample_text() {
    const std::string s =
        "LZDP brick refactor: abcabcabc xyzxyz "
        "streaming design doc alignment test.";
    return {s.begin(), s.end()};
}

int main() {
    using compressor::algorithm::LZDPConfig;
    using compressor::algorithm::pipeline::compress_bytes;
    using compressor::algorithm::pipeline::decompress_bytes;
    using compressor::core_new::LZDPCompressor;
    using compressor::core_new::LZDPCompressorConfig;

    const auto input = sample_text();

    LZDPConfig cfg;
    cfg.encoding.use_flag_encoding = true;

    const auto packed = compress_bytes(input, cfg);
  assert(!packed.compressed.empty());

    const auto restored = decompress_bytes(packed.compressed, cfg);
  assert(restored == input);

    LZDPCompressorConfig file_cfg;
    file_cfg.lzdp = cfg;
    file_cfg.use_streaming = false;
    LZDPCompressor comp(file_cfg);

    const std::string in_path = "test_algo_new_in.bin";
    const std::string out_path = "test_algo_new_out.bin";
    const std::string dec_path = "test_algo_new_dec.bin";

    {
        std::ofstream f(in_path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(input.data()),
                static_cast<std::streamsize>(input.size()));
    }

    comp.compress_file_to_path(in_path, out_path);
    const auto dec = comp.decompress_file(out_path);
  assert(dec == input);

    std::cout << "test_algorithm_new_lzdp: OK\n";
    return 0;
}

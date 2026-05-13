#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include "AlgorithmFactory.hpp"
#include "api.hpp"
int main() {
  namespace fs = std::filesystem;
  std::vector<uint8_t> d(1000, 0xAB);
  fs::path work = fs::path("d:/AAA_C/compression-tool/build_debug/stream_scratch");
  fs::create_directories(work);
  fs::path in = work / "in_dpf.bin";
  fs::path wcx = work / "out_dpf.wcx";
  { std::ofstream f(in, std::ios::binary); f.write((char*)d.data(), d.size()); }
  compressor::core::DpflatePipelineParams df{};
  df.search_size = 2048; df.lookahead_size = 128; df.min_match = 4;
  df.max_chain_length = 128; df.dp_sub_match_max = 6; df.match_engine = 1;
  df.use_flag_encoding = false; df.use_3hfmtree = false; df.huffman_chunk_bits = 8;
  compressor::api::AlgorithmID ch[] = {compressor::api::AlgorithmID::DPFlate};
  auto cr = compressor::api::compressFile(in.string(), wcx.string(), ch, 8192u,
    compressor::core::kFileCompressOptsNone, nullptr, &df);
  std::cout << "success=" << cr.success << " err=" << cr.error_message << std::endl;
  return cr.success ? 0 : 1;
}

#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "LZDP.hpp"
#include "api.hpp"

namespace {

std::vector<uint8_t> read_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool bytes_equal(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size()) == 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string input =
        argc > 1 ? argv[1]
                 : "resources/imdb-movie-reviews-word2vec-tfidf-bow.ipynb";

    compressor::core::LzdpWholeFileParams wf{};
    wf.search_size = 4095;
    wf.lookahead_size = 31;
    wf.min_match = 0;
    wf.dp_top = 6;
    wf.use_flag_encoding = 1;
    wf.match_engine = 1;

    const auto data = read_all(input);
    if (data.empty()) {
        std::cerr << "SKIP: cannot read " << input << "\n";
        return 0;
    }

    namespace pipe = compressor::algorithm::pipeline;
    const compressor::api::AlgorithmID chain[] = {compressor::api::AlgorithmID::LZDP};

    compressor::algorithm::LZDPConfig cfg(
        wf.search_size,
        wf.lookahead_size,
        static_cast<uint8_t>(wf.dp_top),
        compressor::algorithm::models::MatchEngine::HashChain,
        wf.use_flag_encoding != 0,
        wf.min_match);
    if (cfg.encoding.length_bits != 5) {
        std::cerr << "FAIL: length_bits expected 5 for lookahead 31, got "
                  << static_cast<int>(cfg.encoding.length_bits) << "\n";
        return 1;
    }

    const auto mem = pipe::compress_bytes(data, cfg);
    auto mem_dec = pipe::decompress_bytes(mem.compressed, cfg);
    if (!bytes_equal(mem_dec, data)) {
        std::cerr << "FAIL: memory roundtrip size " << mem_dec.size() << " vs " << data.size()
                  << "\n";
        return 1;
    }
    std::cout << "PASS: memory roundtrip (" << data.size() << " bytes)\n";

    const std::string ws = "test_lzdp_imdb_ws";
    const std::string in_path = ws + "/in.bin";
    const std::string stream_path = ws + "/stream.lz";
    const std::string wcx_path = ws + "/out.wcx";

    {
        std::ofstream out(in_path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()),
                  static_cast<std::streamsize>(data.size()));
    }

    pipe::LZDPStreamingOptions opts;
    opts.chunk_size = 512 * 1024;
    opts.workspace_dir = ws;
    pipe::LZDPStreamingPipeline spipe(cfg, opts);
    spipe.compress_file(in_path, stream_path);

    const auto stream_bytes = read_all(stream_path);
    if (!bytes_equal(stream_bytes, mem.compressed)) {
        std::cout << "WARN: stream!=memory compressed (" << stream_bytes.size() << " vs "
                  << mem.compressed.size() << ") — chunk lookahead boundary may differ\n";
    } else {
        std::cout << "PASS: stream==memory compressed (" << stream_bytes.size() << " bytes)\n";
    }

    auto stream_dec = pipe::decompress_bytes(stream_bytes, cfg);
    if (!bytes_equal(stream_dec, data)) {
        std::cerr << "FAIL: stream roundtrip size " << stream_dec.size() << " vs " << data.size()
                  << "\n";
        return 1;
    }
    std::cout << "PASS: stream roundtrip\n";

    auto wcx = compressor::api::compressFile(
        in_path, wcx_path, chain, opts.chunk_size,
        compressor::core::kFileCompressLzdpWholeFileFramed, &wf, nullptr, nullptr, nullptr);
    if (!wcx.success) {
        std::cerr << "FAIL: compressFile: " << wcx.error_message << "\n";
        return 1;
    }

    const auto wcx_bytes = read_all(wcx_path);
    auto unpacked = compressor::api::unpack_wcx(wcx_bytes);
    if (!unpacked.success) {
        std::cerr << "FAIL: unpack_wcx: " << unpacked.error_message << "\n";
        return 1;
    }
    if (!bytes_equal(unpacked.payload, mem.compressed)) {
        std::cerr << "FAIL: GUI-style compressFile payload != memory ("
                  << unpacked.payload.size() << " vs " << mem.compressed.size() << ")\n";
        return 1;
    }
    std::cout << "PASS: compressFile(whole-file flag) payload == memory ("
              << mem.compressed.size() << " bytes)\n";

    const std::string dec_path = ws + "/dec.bin";
    auto dec = compressor::api::decompressFile(
        wcx_path, dec_path, chain, 0, &wf, nullptr, nullptr, nullptr);
    if (!dec.success) {
        std::cerr << "FAIL: decompressFile: " << dec.error_message << "\n";
        return 1;
    }

    const auto dec_data = read_all(dec_path);
    if (!bytes_equal(dec_data, data)) {
        std::cerr << "FAIL: WCX roundtrip size " << dec_data.size() << " vs " << data.size()
                  << "\n";
        return 1;
    }
    std::cout << "PASS: WCX file roundtrip\n";
    return 0;
}

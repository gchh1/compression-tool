/// LZDP memory (`compress_dp`) vs file pipeline (`compressFile`) observability tests.
/// See docs/analysis/lzdp-nonstreaming-vs-streaming-divergence-report.md §6.
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "LZDP.hpp"
#include "api.hpp"

namespace fs = std::filesystem;

namespace {

auto read_all_bytes(const fs::path& p) -> std::vector<uint8_t> {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) {
        return {};
    }
    const auto sz = f.tellg();
    if (sz <= 0) {
        return {};
    }
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(buf.data()),
           static_cast<std::streamsize>(buf.size()));
    return buf;
}

void write_all_bytes(const fs::path& p, const std::vector<uint8_t>& data) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
}

auto make_pattern_corpus() -> std::vector<uint8_t> {
    const std::string pat = "LZDP_PARITY_PATTERN_";
    std::vector<uint8_t> data;
    data.reserve(pat.size() * 800);
    for (int k = 0; k < 800; k++) {
        data.insert(data.end(), pat.begin(), pat.end());
    }
    return data;
}

}  // namespace

int main() {
    using compressor::api::AlgorithmID;
    using compressor::api::compressFile;
    using compressor::api::decompressFile;
    using compressor::api::unpack_wcx;

    compressor::core::LzdpWholeFileParams wf{};
    wf.search_size = 4096;
    wf.lookahead_size = 256;
    wf.min_match = 0;
    wf.dp_top = 3;
    wf.use_flag_encoding = false;
    wf.match_engine = 0;

    const std::vector<uint8_t> data = make_pattern_corpus();

    compressor::algorithm::LZDP lz;
    lz.set_min_match(wf.min_match);
    lz.set_match_engine(wf.match_engine);
    lz.set_use_flag_encoding(wf.use_flag_encoding);
    lz.autoBitWidth(wf.search_size, wf.lookahead_size);
    const std::vector<uint8_t> mem_bytes = [&]() {
        auto dp_result = lz.dp_core(data, wf.search_size, wf.lookahead_size, wf.dp_top);
        return lz.encode_triples(dp_result.triples, lz.get_offset_bits(), lz.get_length_bits(), lz.get_use_flag_encoding());
    }();

    const fs::path work =
        fs::temp_directory_path() /
        ("lzdp_parity_" + std::to_string(static_cast<unsigned long long>(
                               reinterpret_cast<uintptr_t>(&data))));
    fs::create_directories(work);
    const fs::path in_path = work / "input.bin";
    const fs::path out_small_chunk = work / "out_4k.wcx";
    const fs::path out_large_chunk = work / "out_1m.wcx";

    write_all_bytes(in_path, data);

    compressor::core::compression_config().lzdp = wf;
    const AlgorithmID chain[] = {AlgorithmID::LZDP};
    auto r_small = compressFile(in_path.string(), out_small_chunk.string(), chain,
                                4096);
    auto r_large = compressFile(in_path.string(), out_large_chunk.string(), chain,
                                  1024u * 1024u);

    bool ok = true;
    if (!r_small.success) {
        std::cerr << "[parity] compressFile (4KiB chunk) failed: " << r_small.error_message
                  << std::endl;
        ok = false;
    }
    if (!r_large.success) {
        std::cerr << "[parity] compressFile (1MiB chunk) failed: " << r_large.error_message
                  << std::endl;
        ok = false;
    }

    if (ok) {
        const auto bytes_a = read_all_bytes(out_small_chunk);
        const auto bytes_b = read_all_bytes(out_large_chunk);
        if (bytes_a != bytes_b) {
            std::cerr << "[parity] FAIL: stream_chunk invariance — outputs differ ("
                      << bytes_a.size() << " vs " << bytes_b.size() << " bytes)" << std::endl;
            ok = false;
        } else {
            std::cout << "[parity] stream chunk invariance PASS (" << bytes_a.size()
                      << " bytes WCX)" << std::endl;
        }
    }

    if (ok && !mem_bytes.empty()) {
        const auto wcx = read_all_bytes(out_small_chunk);
        const auto unpacked = unpack_wcx(wcx);
        if (!unpacked.success) {
            std::cerr << "[parity] unpack_wcx failed: " << unpacked.error_message << std::endl;
            ok = false;
        } else {
            const size_t mem_sz = mem_bytes.size();
            const size_t pl_sz = unpacked.payload.size();
            std::cout << "[parity] memory compress_dp size=" << mem_sz
                      << "  streaming WCX payload size=" << pl_sz;
            if (mem_sz == pl_sz &&
                std::memcmp(mem_bytes.data(), unpacked.payload.data(), mem_sz) == 0) {
                std::cout << "  (byte-identical payload)" << std::endl;
            } else {
                std::cout << "  (differ — expected per design doc §2)" << std::endl;
            }

            try {
                const std::vector<uint8_t> dec_mem = lz.decompress(mem_bytes);
                if (dec_mem.size() != data.size() || dec_mem != data) {
                    std::cerr << "[parity] FAIL: memory decompress mismatch" << std::endl;
                    ok = false;
                }
            } catch (const std::exception& e) {
                std::cerr << "[parity] FAIL: memory decompress threw: " << e.what() << std::endl;
                ok = false;
            }

            const fs::path dec_stream_path = work / "dec_stream.bin";
            const AlgorithmID dechain[] = {AlgorithmID::LZDPDecompress};
            auto d_stream = decompressFile(out_small_chunk.string(), dec_stream_path.string(),
                                           dechain, 4096);
            if (!d_stream.success) {
                std::cerr << "[parity] FAIL: decompressFile(WCX): " << d_stream.error_message
                          << std::endl;
                ok = false;
            } else {
                const std::vector<uint8_t> dec_pl = read_all_bytes(dec_stream_path);
                if (dec_pl.size() != data.size() || dec_pl != data) {
                    std::cerr << "[parity] FAIL: decompressFile output mismatch" << std::endl;
                    ok = false;
                } else {
                    std::cout << "[parity] decompressFile(WCX) roundtrip OK" << std::endl;
                }
            }
        }
    }

    std::error_code ec;
    fs::remove_all(work, ec);

    if (ok) {
        std::cout << "[parity] ALL CHECKS PASSED" << std::endl;
    }
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

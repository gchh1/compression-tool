#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "LZSScompressor.hpp"
#include "Deflatecompressor.hpp"
#include "LZDPcompressor.hpp"
#include "DPflatecompressor.hpp"
#include "io/FileIO.hpp"

namespace fs = std::filesystem;

static std::vector<uint8_t> read_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

static void write_all(const std::string& path, const std::vector<uint8_t>& data) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(data.data()),
              static_cast<std::streamsize>(data.size()));
}

static std::vector<uint8_t> make_pattern(size_t len) {
    const std::string base = "REGRESS_LZDP_DPFLATE_";
    std::vector<uint8_t> out;
    out.reserve(len);
    while (out.size() < len) {
        for (char c : base) {
            if (out.size() >= len) break;
            out.push_back(static_cast<uint8_t>(c));
        }
    }
    return out;
}

static std::vector<uint8_t> make_random(size_t len, uint64_t seed = 12345) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> dist(0, 255);
    std::vector<uint8_t> out(len);
    for (size_t i = 0; i < len; ++i) {
        out[i] = static_cast<uint8_t>(dist(rng));
    }
    return out;
}

static std::vector<uint8_t> make_text(size_t len) {
    const std::string para =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
        "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. "
        "Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris "
        "nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in "
        "reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla "
        "pariatur. Excepteur sint occaecat cupidatat non proident, sunt in "
        "culpa qui officia deserunt mollit anim id est laborum. ";
    std::vector<uint8_t> out;
    out.reserve(len);
    while (out.size() < len) {
        for (char c : para) {
            if (out.size() >= len) break;
            out.push_back(static_cast<uint8_t>(c));
        }
    }
    return out;
}

static std::vector<uint8_t> make_binary(size_t len) {
    std::vector<uint8_t> out(len);
    for (size_t i = 0; i < len; ++i) {
        out[i] = static_cast<uint8_t>((i * 31 + 7 * ((i >> 3) & 0xFF)) & 0xFF);
    }
    return out;
}

struct TestCase {
    std::string name;
    std::vector<uint8_t> data;
};

static std::vector<TestCase> build_corpora() {
    std::vector<TestCase> cases;
    cases.push_back({"pattern_672", make_pattern(672)});
    cases.push_back({"random_384", make_random(384)});
    cases.push_back({"text_10k", make_text(10000)});
    cases.push_back({"binary_64k", make_binary(65536)});
    return cases;
}

static std::vector<TestCase> build_corpora_fast() {
    std::vector<TestCase> cases;
    cases.push_back({"pattern_672", make_pattern(672)});
    cases.push_back({"random_384", make_random(384)});
    cases.push_back({"text_10k", make_text(10000)});
    return cases;
}

static bool bytes_equal(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size()) == 0;
}

int main() {
    namespace algo = compressor::algorithm;
    namespace pipe = compressor::algorithm::pipeline;
    namespace core = compressor::core_new;

    const auto corpora = build_corpora();

    int passed = 0;
    int failed = 0;

    auto check = [&](const std::string& label, bool ok) {
        if (ok) {
            std::cout << "  PASS: " << label << "\n";
            ++passed;
        } else {
            std::cout << "  FAIL: " << label << "\n";
            ++failed;
        }
    };

    const std::string ws = "test_lzss_deflate_ws";
    fs::create_directories(ws);

    // ===== LZSS Tests =====
    std::cout << "=== LZSS Streaming vs Non-Streaming ===\n";

    for (const auto& tc : corpora) {
        std::cout << "--- " << tc.name << " (" << tc.data.size() << " bytes) ---\n";

        const std::string in_path = ws + "/lzss_" + tc.name + "_in.bin";
        write_all(in_path, tc.data);

        for (int flag : {0, 1}) {
            const std::string flag_str = flag ? "flag_enabled" : "flag_disabled";
            const size_t chunk = 300 * 1024;

            algo::LZSSConfig cfg(4095, 255, 3, flag != 0);

            // Non-streaming
            auto ns_result = pipe::compress_bytes_lzss(tc.data, cfg);
            check("LZSS non-stream compress " + tc.name + " " + flag_str,
                  !ns_result.compressed.empty());
            auto ns_decomp = pipe::decompress_bytes_lzss(ns_result.compressed, cfg);
            check("LZSS non-stream roundtrip " + tc.name + " " + flag_str,
                  bytes_equal(ns_decomp, tc.data));

            // Streaming via pipeline
            const std::string stream_out = ws + "/lzss_" + tc.name + "_" + flag_str + "_stream.bin";
            {
                pipe::LZSSStreamingOptions opts;
                opts.chunk_size = chunk;
                opts.workspace_dir = ws;
                opts.temp_a_name = "lzss_" + tc.name + "_" + flag_str + "_tmp.dp";
                pipe::LZSSStreamingPipeline spipe(cfg, opts);
                spipe.compress_file(in_path, stream_out);
            }
            auto stream_compressed = read_all(stream_out);
            check("LZSS stream compress " + tc.name + " " + flag_str,
                  !stream_compressed.empty());

            // Condition A: streaming == non-streaming compressed output
            check("LZSS stream==nonstream " + tc.name + " " + flag_str,
                  bytes_equal(stream_compressed, ns_result.compressed));

            // Decompress streaming output
            auto stream_decomp = pipe::decompress_bytes_lzss(stream_compressed, cfg);
            check("LZSS stream roundtrip " + tc.name + " " + flag_str,
                  bytes_equal(stream_decomp, tc.data));

            // Test via Compressor API
            {
                core::LZSSCompressorConfig ccfg;
                ccfg.lzss = cfg;
                ccfg.use_streaming = false;
                core::LZSSCompressor comp(ccfg);
                auto api_comp = comp.compress_file(in_path);
                check("LZSS compressor non-stream " + tc.name + " " + flag_str,
                      bytes_equal(api_comp, ns_result.compressed));

                const std::string comp_path =
                    ws + "/lzss_" + tc.name + "_" + flag_str + "_comp.bin";
                comp.compress_file_to_path(in_path, comp_path);
                auto api_dec = comp.decompress_file(comp_path);
                check("LZSS compressor roundtrip " + tc.name + " " + flag_str,
                      bytes_equal(api_dec, tc.data));
            }

            {
                core::LZSSCompressorConfig ccfg;
                ccfg.lzss = cfg;
                ccfg.use_streaming = true;
                ccfg.streaming_chunk_size = chunk;
                ccfg.workspace_dir = ws;
                core::LZSSCompressor comp(ccfg);
                auto api_stream_comp = comp.compress_file(in_path);
                check("LZSS compressor-stream == non-stream " + tc.name + " " + flag_str,
                      bytes_equal(api_stream_comp, ns_result.compressed));

                const std::string comp_s_path =
                    ws + "/lzss_" + tc.name + "_" + flag_str + "_comp_s.bin";
                comp.compress_file_to_path(in_path, comp_s_path);
                auto api_dec = comp.decompress_file(comp_s_path);
                check("LZSS compressor-stream roundtrip " + tc.name + " " + flag_str,
                      bytes_equal(api_dec, tc.data));
            }

            // Decompress to path (分块写盘)
            {
                core::LZSSCompressorConfig ccfg;
                ccfg.lzss = cfg;
                ccfg.use_streaming = true;
                ccfg.streaming_chunk_size = chunk;
                ccfg.workspace_dir = ws;
                core::LZSSCompressor comp(ccfg);

                const std::string comp_path = ws + "/lzss_" + tc.name + "_" + flag_str + "_dcp.bin";
                const std::string dec_path = ws + "/lzss_" + tc.name + "_" + flag_str + "_dcp_dec.bin";
                comp.compress_file_to_path(in_path, comp_path);
                comp.decompress_file_to_path(comp_path, dec_path, 4096);
                auto dec_data = read_all(dec_path);
                check("LZSS decomp_to_path roundtrip " + tc.name + " " + flag_str,
                      bytes_equal(dec_data, tc.data));
            }
        }
    }

    // ===== Deflate Tests =====
    std::cout << "\n=== Deflate Streaming vs Non-Streaming ===\n";

    for (const auto& tc : corpora) {
        std::cout << "--- " << tc.name << " (" << tc.data.size() << " bytes) ---\n";

        const std::string in_path = ws + "/deflate_" + tc.name + "_in.bin";
        write_all(in_path, tc.data);

        const size_t chunk = 300 * 1024;

        algo::DeflateConfig cfg(16384, 258, 3, false, 8, 8, true);
        {
            const std::string flag_str = "flag_enabled";

            // Non-streaming
            auto ns_result = pipe::compress_bytes_deflate(tc.data, cfg);
            check("Deflate non-stream compress " + tc.name + " " + flag_str,
                  !ns_result.compressed.empty());
            auto ns_decomp = pipe::decompress_bytes_deflate(ns_result.compressed, cfg);
            check("Deflate non-stream roundtrip " + tc.name + " " + flag_str,
                  bytes_equal(ns_decomp, tc.data));

            // Streaming via pipeline
            const std::string stream_out =
                ws + "/deflate_" + tc.name + "_" + flag_str + "_stream.bin";
            {
                pipe::DeflateStreamingOptions opts;
                opts.chunk_size = chunk;
                opts.workspace_dir = ws;
                opts.temp_a_name = "deflate_" + tc.name + "_" + flag_str + "_tmp.dp";
                pipe::DeflateStreamingPipeline spipe(cfg, opts);
                spipe.compress_file(in_path, stream_out);
            }
            auto stream_compressed = read_all(stream_out);
            check("Deflate stream compress " + tc.name + " " + flag_str,
                  !stream_compressed.empty());

            // Condition A: streaming == non-streaming compressed output
            check("Deflate stream==nonstream " + tc.name + " " + flag_str,
                  bytes_equal(stream_compressed, ns_result.compressed));

            // Decompress streaming output
            auto stream_decomp = pipe::decompress_bytes_deflate(stream_compressed, cfg);
            check("Deflate stream roundtrip " + tc.name + " " + flag_str,
                  bytes_equal(stream_decomp, tc.data));

            // Test via Compressor API
            {
                core::DeflateCompressorConfig ccfg;
                ccfg.deflate = cfg;
                ccfg.use_streaming = false;
                core::DeflateCompressor comp(ccfg);
                auto api_comp = comp.compress_file(in_path);
                check("Deflate compressor non-stream " + tc.name + " " + flag_str,
                      bytes_equal(api_comp, ns_result.compressed));

                const std::string comp_path =
                    ws + "/deflate_" + tc.name + "_" + flag_str + "_comp.bin";
                comp.compress_file_to_path(in_path, comp_path);
                auto api_dec = comp.decompress_file(comp_path);
                check("Deflate compressor roundtrip " + tc.name + " " + flag_str,
                      bytes_equal(api_dec, tc.data));
            }

            {
                core::DeflateCompressorConfig ccfg;
                ccfg.deflate = cfg;
                ccfg.use_streaming = true;
                ccfg.streaming_chunk_size = chunk;
                ccfg.workspace_dir = ws;
                core::DeflateCompressor comp(ccfg);
                auto api_stream_comp = comp.compress_file(in_path);
                check("Deflate compressor-stream == non-stream " + tc.name + " " + flag_str,
                      bytes_equal(api_stream_comp, ns_result.compressed));

                const std::string comp_s_path =
                    ws + "/deflate_" + tc.name + "_" + flag_str + "_comp_s.bin";
                comp.compress_file_to_path(in_path, comp_s_path);
                auto api_dec = comp.decompress_file(comp_s_path);
                check("Deflate compressor-stream roundtrip " + tc.name + " " + flag_str,
                      bytes_equal(api_dec, tc.data));
            }

            // Decompress to path (分块写盘)
            {
                core::DeflateCompressorConfig ccfg;
                ccfg.deflate = cfg;
                ccfg.use_streaming = true;
                ccfg.streaming_chunk_size = chunk;
                ccfg.workspace_dir = ws;
                core::DeflateCompressor comp(ccfg);

                const std::string comp_path =
                    ws + "/deflate_" + tc.name + "_" + flag_str + "_dcp.bin";
                const std::string dec_path =
                    ws + "/deflate_" + tc.name + "_" + flag_str + "_dcp_dec.bin";
                comp.compress_file_to_path(in_path, comp_path);
                comp.decompress_file_to_path(comp_path, dec_path, 4096);
                auto dec_data = read_all(dec_path);
                check("Deflate decomp_to_path roundtrip " + tc.name + " " + flag_str,
                      bytes_equal(dec_data, tc.data));
            }
        }

        // Deflate without flag encoding
        {
            algo::DeflateConfig cfg_nf(16384, 258, 3, false, 8, 8, false);
            const std::string flag_str = "flag_disabled";

            auto ns_result = pipe::compress_bytes_deflate(tc.data, cfg_nf);
            check("Deflate non-stream compress " + tc.name + " " + flag_str,
                  !ns_result.compressed.empty());
            auto ns_decomp = pipe::decompress_bytes_deflate(ns_result.compressed, cfg_nf);
            check("Deflate non-stream roundtrip " + tc.name + " " + flag_str,
                  bytes_equal(ns_decomp, tc.data));

            const std::string stream_out =
                ws + "/deflate_" + tc.name + "_" + flag_str + "_stream.bin";
            {
                pipe::DeflateStreamingOptions opts;
                opts.chunk_size = chunk;
                opts.workspace_dir = ws;
                opts.temp_a_name = "deflate_" + tc.name + "_" + flag_str + "_tmp.dp";
                pipe::DeflateStreamingPipeline spipe(cfg_nf, opts);
                spipe.compress_file(in_path, stream_out);
            }
            auto stream_compressed = read_all(stream_out);
            check("Deflate stream==nonstream " + tc.name + " " + flag_str,
                  bytes_equal(stream_compressed, ns_result.compressed));
            auto stream_decomp = pipe::decompress_bytes_deflate(stream_compressed, cfg_nf);
            check("Deflate stream roundtrip " + tc.name + " " + flag_str,
                  bytes_equal(stream_decomp, tc.data));
        }
    }

    // ===== LZDP Tests (D1-D6) =====
    std::cout << "\n=== LZDP Streaming vs Non-Streaming ===\n";

    {
        const auto lzdp_corpora = build_corpora_fast();
        for (const auto& tc : lzdp_corpora) {
        std::cout << "--- " << tc.name << " (" << tc.data.size() << " bytes) ---\n";

        const std::string in_path = ws + "/lzdp_" + tc.name + "_in.bin";
        write_all(in_path, tc.data);

        for (int flag : {0, 1}) {
            const std::string flag_str = flag ? "flag_enabled" : "flag_disabled";
            const size_t chunk = 300 * 1024;

            algo::LZDPConfig cfg(4095, 255, 3, algo::models::MatchEngine::HashChain, flag != 0);

            auto ns_result = pipe::compress_bytes(tc.data, cfg);
            check("LZDP non-stream compress " + tc.name + " " + flag_str,
                  !ns_result.compressed.empty());
            auto ns_decomp = pipe::decompress_bytes(ns_result.compressed, cfg);
            check("LZDP non-stream roundtrip " + tc.name + " " + flag_str,
                  bytes_equal(ns_decomp, tc.data));

            try {
                const std::string stream_out = ws + "/lzdp_" + tc.name + "_" + flag_str + "_stream.bin";
                {
                    pipe::LZDPStreamingOptions opts;
                    opts.chunk_size = chunk;
                    opts.workspace_dir = ws;
                    opts.temp_a_name = "lzdp_" + tc.name + "_" + flag_str + "_a.dp";
                    opts.temp_b_name = "lzdp_" + tc.name + "_" + flag_str + "_b.tok";
                    pipe::LZDPStreamingPipeline spipe(cfg, opts);
                    spipe.compress_file(in_path, stream_out);
                }
                auto stream_compressed = read_all(stream_out);
                check("LZDP stream compress " + tc.name + " " + flag_str,
                      !stream_compressed.empty());

                check("LZDP stream==nonstream " + tc.name + " " + flag_str,
                      bytes_equal(stream_compressed, ns_result.compressed));

                auto stream_decomp = pipe::decompress_bytes(stream_compressed, cfg);
                check("LZDP stream roundtrip " + tc.name + " " + flag_str,
                      bytes_equal(stream_decomp, tc.data));
            } catch (const std::exception& e) {
                std::cout << "  FAIL: LZDP stream " + tc.name + " " + flag_str
                          << " (" << e.what() << ")\n";
                failed += 3;
            }

            {
                core::LZDPCompressorConfig ccfg;
                ccfg.lzdp = cfg;
                ccfg.use_streaming = false;
                core::LZDPCompressor comp(ccfg);
                auto api_comp = comp.compress_file(in_path);
                check("LZDP compressor non-stream " + tc.name + " " + flag_str,
                      bytes_equal(api_comp, ns_result.compressed));

                const std::string comp_path =
                    ws + "/lzdp_" + tc.name + "_" + flag_str + "_comp.bin";
                comp.compress_file_to_path(in_path, comp_path);
                auto api_dec = comp.decompress_file(comp_path);
                check("LZDP compressor roundtrip " + tc.name + " " + flag_str,
                      bytes_equal(api_dec, tc.data));
            }

            try {
                core::LZDPCompressorConfig ccfg;
                ccfg.lzdp = cfg;
                ccfg.use_streaming = true;
                ccfg.streaming_chunk_size = chunk;
                ccfg.workspace_dir = ws;
                core::LZDPCompressor comp(ccfg);
                auto api_stream_comp = comp.compress_file(in_path);
                check("LZDP compressor-stream == non-stream " + tc.name + " " + flag_str,
                      bytes_equal(api_stream_comp, ns_result.compressed));

                const std::string comp_s_path =
                    ws + "/lzdp_" + tc.name + "_" + flag_str + "_comp_s.bin";
                comp.compress_file_to_path(in_path, comp_s_path);
                auto api_dec = comp.decompress_file(comp_s_path);
                check("LZDP compressor-stream roundtrip " + tc.name + " " + flag_str,
                      bytes_equal(api_dec, tc.data));
            } catch (const std::exception& e) {
                std::cout << "  FAIL: LZDP compressor-stream " + tc.name + " " + flag_str
                          << " (" << e.what() << ")\n";
                failed += 2;
            }
        }
    }
    }

    // ===== DPFlate Tests (P1-P12) =====
    std::cout << "\n=== DPFlate Streaming vs Non-Streaming ===\n";

    {
        const auto dpf_corpora = build_corpora_fast();
        for (const auto& tc : dpf_corpora) {
        std::cout << "--- " << tc.name << " (" << tc.data.size() << " bytes) ---\n";

        const std::string in_path = ws + "/dpflate_" + tc.name + "_in.bin";
        write_all(in_path, tc.data);

        const size_t chunk = 300 * 1024;

        for (int use3hfm : {0, 1}) {
            const std::string tree_str = use3hfm ? "3HfMT" : "FLATE";

            for (int flag : {0, 1}) {
                const std::string flag_str = flag ? "flag_enabled" : "flag_disabled";

                algo::DPflateConfig cfg(4095, 255, 3,
                    algo::models::MatchEngine::HashChain, flag != 0,
                    use3hfm != 0, 8, 8);

                auto ns_result = pipe::compress_bytes_dpflate(tc.data, cfg);
                check("DPFlate non-stream compress " + tc.name + " " + tree_str + " " + flag_str,
                      !ns_result.compressed.empty());
                auto ns_decomp = pipe::decompress_bytes_dpflate(ns_result.compressed, cfg);
                check("DPFlate non-stream roundtrip " + tc.name + " " + tree_str + " " + flag_str,
                      bytes_equal(ns_decomp, tc.data));

                try {
                    const std::string stream_out =
                        ws + "/dpflate_" + tc.name + "_" + tree_str + "_" + flag_str + "_stream.bin";
                    {
                        pipe::DPFlateStreamingOptions opts;
                        opts.chunk_size = chunk;
                        opts.workspace_dir = ws;
                        opts.temp_a_name = "dpflate_" + tc.name + "_" + tree_str + "_" + flag_str + "_a.dp";
                        opts.temp_b_name = "dpflate_" + tc.name + "_" + tree_str + "_" + flag_str + "_b.tok";
                        pipe::DPFlateStreamingPipeline spipe(cfg, opts);
                        spipe.compress_file(in_path, stream_out);
                    }
                    auto stream_compressed = read_all(stream_out);
                    check("DPFlate stream compress " + tc.name + " " + tree_str + " " + flag_str,
                          !stream_compressed.empty());

                    check("DPFlate stream==nonstream " + tc.name + " " + tree_str + " " + flag_str,
                          bytes_equal(stream_compressed, ns_result.compressed));

                    auto stream_decomp = pipe::decompress_bytes_dpflate(stream_compressed, cfg);
                    check("DPFlate stream roundtrip " + tc.name + " " + tree_str + " " + flag_str,
                          bytes_equal(stream_decomp, tc.data));
                } catch (const std::exception& e) {
                    std::cout << "  FAIL: DPFlate stream " + tc.name + " " + tree_str + " " + flag_str
                              << " (" << e.what() << ")\n";
                    failed += 3;
                }

                {
                    core::DPflateCompressorConfig ccfg;
                    ccfg.dpflate = cfg;
                    ccfg.use_streaming = false;
                    core::DPflateCompressor comp(ccfg);
                    auto api_comp = comp.compress_file(in_path);
                    check("DPFlate compressor non-stream " + tc.name + " " + tree_str + " " + flag_str,
                          bytes_equal(api_comp, ns_result.compressed));

                    const std::string comp_path =
                        ws + "/dpflate_" + tc.name + "_" + tree_str + "_" + flag_str + "_comp.bin";
                    comp.compress_file_to_path(in_path, comp_path);
                    auto api_dec = comp.decompress_file(comp_path);
                    check("DPFlate compressor roundtrip " + tc.name + " " + tree_str + " " + flag_str,
                          bytes_equal(api_dec, tc.data));
                }

                try {
                    core::DPflateCompressorConfig ccfg;
                    ccfg.dpflate = cfg;
                    ccfg.use_streaming = true;
                    ccfg.streaming_chunk_size = chunk;
                    ccfg.workspace_dir = ws;
                    core::DPflateCompressor comp(ccfg);
                    auto api_stream_comp = comp.compress_file(in_path);
                    check("DPFlate compressor-stream == non-stream " + tc.name + " " + tree_str + " " + flag_str,
                          bytes_equal(api_stream_comp, ns_result.compressed));

                    const std::string comp_s_path =
                        ws + "/dpflate_" + tc.name + "_" + tree_str + "_" + flag_str + "_comp_s.bin";
                    comp.compress_file_to_path(in_path, comp_s_path);
                    auto api_dec = comp.decompress_file(comp_s_path);
                    check("DPFlate compressor-stream roundtrip " + tc.name + " " + tree_str + " " + flag_str,
                          bytes_equal(api_dec, tc.data));
                } catch (const std::exception& e) {
                    std::cout << "  FAIL: DPFlate compressor-stream " + tc.name + " " + tree_str + " " + flag_str
                              << " (" << e.what() << ")\n";
                    failed += 2;
                }
            }
        }
    }
    }

    // ===== Summary =====
    std::cout << "\n=== SUMMARY ===\n";
    std::cout << "PASS: " << passed << "\n";
    std::cout << "FAIL: " << failed << "\n";

    return failed > 0 ? 1 : 0;
}
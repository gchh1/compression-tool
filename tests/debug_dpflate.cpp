#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <string>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <span>
#include "DPFlateCompressor.hpp"
#include "DPFlate.hpp"
#include "Inflate.hpp"
#include "Inflate3HM.hpp"
#include "IAlgorithm.hpp"
#include "api.hpp"
#include "DebugLog.hpp"

namespace fs = std::filesystem;

static std::vector<uint8_t> read_file(const fs::path& p) {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto sz = f.tellg();
    if (sz <= 0) return {};
    f.seekg(0);
    std::vector<uint8_t> b(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()));
    return b;
}

static void write_file(const fs::path& p, const std::vector<uint8_t>& d) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(d.data()), static_cast<std::streamsize>(d.size()));
}

static void hex_dump(const std::vector<uint8_t>& d, size_t max_bytes = 64) {
    for (size_t i = 0; i < d.size() && i < max_bytes; ++i) {
        printf("%02x ", d[i]);
        if ((i + 1) % 16 == 0) printf("\n  ");
    }
    printf("\n");
}

// Direct Inflate decompression (no DPFlateCompressor wrapper)
static std::vector<uint8_t> inflate_direct(const std::vector<uint8_t>& compressed) {
    compressor::algorithm::Inflate dec;
    dec.reset();
    std::vector<uint8_t> out(std::max(compressed.size() * 10 + 65536, size_t{4096}));
    size_t in_off = 0;
    size_t out_pos = 0;
    compressor::algorithm::AlgorithmStatus st{};
    for (;;) {
        if (out_pos >= out.size()) {
            out.resize(std::max(out.size() * 2, out_pos + compressed.size() + 65536));
        }
        auto in_span = std::span<const uint8_t>(compressed.data() + in_off, compressed.size() - in_off);
        auto out_span = std::span<uint8_t>(out.data() + out_pos, out.size() - out_pos);
        st = dec.process(in_span, out_span, true);
        in_off += st.bytes_consumed;
        out_pos += st.bytes_produced;
        if (st.done) break;
        if (st.need_output && st.bytes_produced == 0) {
            out.resize(std::max(out.size() * 2, out_pos + compressed.size() + 65536));
            continue;
        }
        if (st.need_input && in_off >= compressed.size()) break;
    }
    out.resize(out_pos);
    return out;
}

int main() {
    compressor::debug::DebugLog::instance().enable("debug_3hm.log");
    const std::string pat = "REGRESS_LZDP_DPFLATE_";
    std::vector<uint8_t> data;
    for (int i = 0; i < 32; ++i)
        data.insert(data.end(), pat.begin(), pat.end());

    printf("Original size: %zu\n\n", data.size());

    // ---- Test 0: Simple pattern test ----
    printf("=== Simple pattern test ===\n");
    {
        // Very simple repeating pattern
        std::vector<uint8_t> simple;
        const char sp[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        for (int i = 0; i < 10; ++i)
            simple.insert(simple.end(), sp, sp + sizeof(sp) - 1);
        printf("  Simple size: %zu\n", simple.size());

        compressor::core::DPFlateCompressor comp;
        comp.set_search_size(2048);
        comp.set_lookahead_size(128);
        comp.set_min_match(4);
        comp.set_max_chain_length(128);
        comp.set_dp_sub_match_max(6);
        comp.set_match_engine(1);
        comp.set_use_flag_encoding(false);
        comp.set_use_3hfmtree(false);
        auto cr = comp.compress(simple);
        printf("  Compressed: %zu bytes\n", cr.data.size());
        auto dec = comp.decompress(cr.data);
        printf("  Decompressed: %zu bytes, success=%d\n", dec.data.size(), dec.success);
        printf("  Match: %s\n", dec.data == simple ? "YES" : "NO");
        if (dec.data != simple) {
            for (size_t i = 0; i < simple.size() && i < dec.data.size(); ++i) {
                if (simple[i] != dec.data[i]) {
                    printf("  First diff at offset %zu: expected 0x%02x got 0x%02x\n",
                           i, simple[i], dec.data[i]);
                    break;
                }
            }
        }
    }
    printf("\n");

    // ---- Test 1: DPFlateCompressor direct (memory mode) ----
    printf("=== DPFlateCompressor (memory) ===\n");
    std::vector<uint8_t> mem_compressed;
    {
        compressor::core::DPFlateCompressor comp;
        comp.set_search_size(2048);
        comp.set_lookahead_size(128);
        comp.set_min_match(4);
        comp.set_max_chain_length(128);
        comp.set_dp_sub_match_max(6);
        comp.set_match_engine(1);
        comp.set_use_flag_encoding(false);
        comp.set_use_3hfmtree(false);
        auto cr = comp.compress(data);
        mem_compressed = cr.data;
        printf("  Compressed: %zu bytes\n", cr.data.size());
        printf("  First 64 bytes:\n  ");
        hex_dump(cr.data, 64);

        auto dec = comp.decompress(cr.data);
        printf("  Decompressed: %zu bytes, success=%d\n", dec.data.size(), dec.success);
        printf("  Match: %s\n", dec.data == data ? "YES" : "NO");
        if (dec.data != data) {
            for (size_t i = 0; i < data.size() && i < dec.data.size(); ++i) {
                if (data[i] != dec.data[i]) {
                    printf("  First diff at offset %zu: expected 0x%02x ('%c') got 0x%02x\n",
                           i, data[i], data[i] >= 32 ? data[i] : '?', dec.data[i]);
                    // Show context around the diff
                    size_t start = (i > 16) ? i - 16 : 0;
                    size_t end = std::min(i + 16, data.size());
                    printf("  Context original [%zu..%zu]:\n  ", start, end - 1);
                    for (size_t j = start; j < end; ++j) {
                        if (j == i) printf(" [%02x]", data[j]);
                        else printf(" %02x", data[j]);
                    }
                    printf("\n  Context decoded  [%zu..%zu]:\n  ", start, end - 1);
                    for (size_t j = start; j < end && j < dec.data.size(); ++j) {
                        if (j == i) printf(" [%02x]", dec.data[j]);
                        else printf(" %02x", dec.data[j]);
                    }
                    printf("\n");
                    break;
                }
            }
        }
    }

    // ---- Test 1b: Direct Inflate on memory compressed data (skip format byte) ----
    printf("\n=== Direct Inflate on memory compressed (skip format byte) ===\n");
    if (mem_compressed.size() >= 2) {
        printf("  Format byte: 0x%02x\n", mem_compressed[0]);
        std::vector<uint8_t> payload(mem_compressed.begin() + 1, mem_compressed.end());
        auto dec = inflate_direct(payload);
        printf("  Decompressed: %zu bytes\n", dec.size());
        printf("  Match: %s\n", dec == data ? "YES" : "NO");
        if (dec != data) {
            for (size_t i = 0; i < data.size() && i < dec.size(); ++i) {
                if (data[i] != dec[i]) {
                    printf("  First diff at offset %zu: expected 0x%02x got 0x%02x\n",
                           i, data[i], dec[i]);
                    break;
                }
            }
        }
    }

    // ---- Test 2: compressFile/decompressFile (streaming API) ----
    printf("\n=== compressFile/decompressFile (streaming) ===\n");
    {
        FILE* log = fopen("streaming_test.log", "w");
        if (log) {
            fprintf(log, "=== streaming test started ===\n");
            fflush(log);
        }
#define LOGF(...) do { printf(__VA_ARGS__); if(log){fprintf(log,__VA_ARGS__);fflush(log);} } while(0)

        using compressor::api::AlgorithmID;
        using compressor::api::compressFile;
        using compressor::api::decompressFile;

        // Use a unique temp directory to avoid permission issues from stale locks
        const fs::path work = fs::temp_directory_path() / ("debug_dpflate_work_" + std::to_string(rand()));
        std::error_code ec;
        fs::remove_all(work, ec);
        fs::create_directories(work, ec);

        const fs::path in = work / "in.bin";
        const fs::path wcx = work / "out.wcx";
        const fs::path dec = work / "dec.bin";
        write_file(in, data);

        compressor::core::DpflatePipelineParams df{};
        df.search_size = 2048;
        df.lookahead_size = 128;
        df.min_match = 4;
        df.max_chain_length = 128;
        df.dp_sub_match_max = 6;
        df.match_engine = 1;
        df.use_flag_encoding = false;
        df.use_3hfmtree = false;

        const AlgorithmID ch[] = {AlgorithmID::DPFlate};
        LOGF("  Calling compressFile...\n");
        auto cr = compressFile(in.string(), wcx.string(), ch, 8192u,
                               compressor::core::kFileCompressOptsNone, nullptr, &df);
        LOGF("  compressFile returned, success=%d\n", cr.success);
        if (!cr.success) {
            LOGF("  Error: %s\n", cr.error_message.c_str());
        } else {
            auto compressed = read_file(wcx);
            LOGF("  Compressed file: %zu bytes (includes WCX header)\n", compressed.size());
            
            // Find the DPFlate payload inside the WCX container
            size_t payload_start = 0;
            for (size_t i = 0; i + 1 < compressed.size(); ++i) {
                if (compressed[i] == 0x46 || compressed[i] == 0x33) {
                    payload_start = i;
                    break;
                }
            }
            LOGF("  DPFlate payload starts at offset %zu\n", payload_start);
            LOGF("  First 64 bytes of payload:\n  ");
            if (log) {
                for (size_t i = 0; i < compressed.size() && i < 64; ++i) {
                    fprintf(log, "%02x ", compressed[i]);
                    if ((i + 1) % 16 == 0) fprintf(log, "\n  ");
                }
                fprintf(log, "\n");
            }

            // Compare with memory mode compressed data
            LOGF("\n  Comparing streaming payload vs memory compressed:\n");
            size_t compare_len = std::min(mem_compressed.size(), compressed.size() - payload_start);
            bool identical = true;
            for (size_t i = 0; i < compare_len; ++i) {
                if (mem_compressed[i] != compressed[payload_start + i]) {
                    LOGF("  First diff at offset %zu: mem=0x%02x stream=0x%02x\n",
                           i, mem_compressed[i], compressed[payload_start + i]);
                    identical = false;
                    break;
                }
            }
            if (identical) {
                LOGF("  Streaming payload == Memory compressed: YES\n");
            }

            LOGF("  About to call decompressFile...\n");
            const AlgorithmID de[] = {AlgorithmID::Inflate};
            auto dr = decompressFile(wcx.string(), dec.string(), de, 8192u);
            LOGF("  decompressFile returned, success=%d\n", dr.success);
            if (!dr.success) {
                LOGF("  Error: %s\n", dr.error_message.c_str());
            } else {
                auto dec_data = read_file(dec);
                LOGF("  Decompressed: %zu bytes\n", dec_data.size());
                LOGF("  Match: %s\n", dec_data == data ? "YES" : "NO");
                if (dec_data != data) {
                    for (size_t i = 0; i < data.size() && i < dec_data.size(); ++i) {
                        if (data[i] != dec_data[i]) {
                            LOGF("  First diff at offset %zu: expected 0x%02x got 0x%02x\n",
                                   i, data[i], dec_data[i]);
                            break;
                        }
                    }
                }
            }
        }
        fs::remove_all(work, ec);
        if (log) fclose(log);
    }

    // ---- Test 3: DPFlateCompressor with 3HfMT ----
    printf("\n=== DPFlateCompressor 3HfMT (memory) ===\n");
    {
        compressor::core::DPFlateCompressor comp;
        comp.set_search_size(2048);
        comp.set_lookahead_size(128);
        comp.set_min_match(4);
        comp.set_max_chain_length(128);
        comp.set_dp_sub_match_max(6);
        comp.set_match_engine(1);
        comp.set_use_flag_encoding(false);
        comp.set_use_3hfmtree(true);
        auto cr = comp.compress(data);
        printf("  Compressed: %zu bytes\n", cr.data.size());
        printf("  First 64 bytes:\n  ");
        hex_dump(cr.data, 64);

        auto dec = comp.decompress(cr.data);
        printf("  Decompressed: %zu bytes, success=%d\n", dec.data.size(), dec.success);
        printf("  Match: %s\n", dec.data == data ? "YES" : "NO");
        if (dec.data != data) {
            for (size_t i = 0; i < data.size() && i < dec.data.size(); ++i) {
                if (data[i] != dec.data[i]) {
                    printf("  First diff at offset %zu: expected 0x%02x got 0x%02x\n",
                           i, data[i], dec.data[i]);
                    break;
                }
            }
        }
    }

    // ---- Test 3b: Direct Inflate3HM on 3HfMT compressed data ----
    printf("\n=== Direct Inflate3HM on 3HfMT compressed (skip format byte) ===\n");
    {
        compressor::core::DPFlateCompressor comp;
        comp.set_search_size(2048);
        comp.set_lookahead_size(128);
        comp.set_min_match(4);
        comp.set_max_chain_length(128);
        comp.set_dp_sub_match_max(6);
        comp.set_match_engine(1);
        comp.set_use_flag_encoding(false);
        comp.set_use_3hfmtree(true);
        auto cr = comp.compress(data);
        
        if (cr.data.size() >= 2 && cr.data[0] == 0x33) {
            printf("  Format byte: 0x33\n");
            std::vector<uint8_t> payload(cr.data.begin() + 1, cr.data.end());
            
            compressor::algorithm::Inflate3HM dec;
            dec.reset();
            std::vector<uint8_t> out(std::max(payload.size() * 10 + 65536, size_t{4096}));
            size_t in_off = 0;
            size_t out_pos = 0;
            compressor::algorithm::AlgorithmStatus st{};
            for (;;) {
                if (out_pos >= out.size()) {
                    out.resize(std::max(out.size() * 2, out_pos + payload.size() + 65536));
                }
                auto in_span = std::span<const uint8_t>(payload.data() + in_off, payload.size() - in_off);
                auto out_span = std::span<uint8_t>(out.data() + out_pos, out.size() - out_pos);
                st = dec.process(in_span, out_span, true);
                in_off += st.bytes_consumed;
                out_pos += st.bytes_produced;
                if (st.done) break;
                if (st.need_output && st.bytes_produced == 0) {
                    out.resize(std::max(out.size() * 2, out_pos + payload.size() + 65536));
                    continue;
                }
                if (st.need_input && in_off >= payload.size()) break;
            }
            out.resize(out_pos);
            printf("  Decompressed: %zu bytes, done=%d\n", out.size(), st.done);
            printf("  Match: %s\n", out == data ? "YES" : "NO");
            if (out != data) {
                for (size_t i = 0; i < data.size() && i < out.size(); ++i) {
                    if (data[i] != out[i]) {
                        printf("  First diff at offset %zu: expected 0x%02x got 0x%02x\n",
                               i, data[i], out[i]);
                        break;
                    }
                }
            }
        }
    }

    return 0;
}
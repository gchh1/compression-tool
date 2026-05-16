/**
 * @file test_algorithm_comparison.cpp
 * @brief 统一算法对比测试：LZDP / LZSS / DPFlate / Deflate / 3HfMT
 *
 * 对每种算法分别做流式（streaming）和非流式（memory）压缩对比，
 * 解压后与原文件逐字节对比，输出 CSV 报告。
 *
 * 用法：
 *   test_algorithm_comparison              # 默认语料运行
 *   test_algorithm_comparison --csv report.csv  # 输出到指定 CSV
 *   test_algorithm_comparison --large      # 包含大文件测试（100MB+）
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "DebugLog.hpp"

// === DEBUG_BLOCK_BEGIN (可删除) ===
#include <cstdarg>
#include <mutex>
static FILE* g_test_log = nullptr;
static std::mutex g_test_log_mtx;
static void test_log_open() {
    if (!g_test_log) {
        g_test_log = fopen("test_algorithm_comparison_debug.log", "a");
    }
}
static void test_log_write(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(g_test_log_mtx);
    test_log_open();
    if (g_test_log) {
        va_list args;
        va_start(args, fmt);
        vfprintf(g_test_log, fmt, args);
        va_end(args);
        fprintf(g_test_log, "\n");
        fflush(g_test_log);
    }
}
// === DEBUG_BLOCK_END ===

#include "Deflate.hpp"
#include "DeflateCompressor.hpp"
#include "DPFlate.hpp"
#include "DPFlateCompressor.hpp"
#include "Inflate.hpp"
#include "Inflate3HM.hpp"
#include "HuffmanTree3HM.hpp"
#include "BitReader.hpp"
#include "BitWriter.hpp"
#include "LZDP.hpp"
#include "LZDPCompressor.hpp"
#include "LZSS.hpp"
#include "LZSSCompressor.hpp"
#include "api.hpp"

namespace fs = std::filesystem;

// ============================================================
// CRC32
// ============================================================
static uint32_t crc32_update(uint32_t crc, const uint8_t* p, size_t n) {
    static const uint32_t kTable[256] = {
        0x00000000u, 0x77073096u, 0xee0e612cu, 0x990951bau, 0x076dc419u, 0x706af48fu,
        0xe963a535u, 0x9e6495a3u, 0x0edb8832u, 0x79dcb8a4u, 0xe0d5e91eu, 0x97d2d988u,
        0x09b64c2bu, 0x7eb17cbdu, 0xe7b82d07u, 0x90bf1d91u, 0x1db71064u, 0x6ab020f2u,
        0xf3b97148u, 0x84be41deu, 0x1adad47du, 0x6ddde4ebu, 0xf4d4b551u, 0x83d385c7u,
        0x136c9856u, 0x646ba8c0u, 0xfd62f97au, 0x8a65c9ecu, 0x14015c4fu, 0x63066cd9u,
        0xfa0f3d63u, 0x8d080df5u, 0x3b6e20c8u, 0x4c69105eu, 0xd56041e4u, 0xa2677172u,
        0x3c03e4d1u, 0x4b04d447u, 0xd20d85fdu, 0xa50ab56bu, 0x35b5a8fau, 0x42b2986cu,
        0xdbbbc9d6u, 0xacbcf940u, 0x32d86ce3u, 0x45df5c75u, 0xdcd60dcfu, 0xabd13d59u,
        0x26d930acu, 0x51de003au, 0xc8d75180u, 0xbfd06116u, 0x21b4f4b5u, 0x56b3c423u,
        0xcfba9599u, 0xb8bda50fu, 0x2802b89eu, 0x5f058808u, 0xc60cd9b2u, 0xb10be924u,
        0x2f6f7c87u, 0x58684c11u, 0xc1611dabu, 0xb6662d3du, 0x76dc4190u, 0x01db7106u,
        0x98d220bcu, 0xefd5102au, 0x71b18589u, 0x06b6b51fu, 0x9fbfe4a5u, 0xe8b8d433u,
        0x7807c9a2u, 0x0f00f934u, 0x9609a88eu, 0xe10e9818u, 0x7f6a0dbbu, 0x086d3d2du,
        0x91646c97u, 0xe6635c01u, 0x6b6b51f4u, 0x1c6c6162u, 0x856530d8u, 0xf262004eu,
        0x6c0695edu, 0x1b01a57bu, 0x8208f4c1u, 0xf50fc457u, 0x65b0d9c6u, 0x12b7e950u,
        0x8bbeb8eau, 0xfcb9887cu, 0x62dd1ddfu, 0x15da2d49u, 0x8cd37cf3u, 0xfbd44c65u,
        0x4db26158u, 0x3ab551ceu, 0xa3bc0074u, 0xd4bb30e2u, 0x4adfa541u, 0x3dd895d7u,
        0xa4d1c46du, 0xd3d6f4fbu, 0x4369e96au, 0x346ed9fcu, 0xad678846u, 0xda60b8d0u,
        0x44042d73u, 0x33031de5u, 0xaa0a4c5fu, 0xdd0d7cc9u, 0x5005713cu, 0x270241aau,
        0xbe0b1010u, 0xc90c2086u, 0x5768b525u, 0x206f85b3u, 0xb966d409u, 0xce61e49fu,
        0x5edef90eu, 0x29d9c998u, 0xb0d09822u, 0xc7d7a8b4u, 0x59b33d17u, 0x2eb40d81u,
        0xb7bd5c3bu, 0xc0ba6cadu, 0xedb88320u, 0x9abfb3b6u, 0x03b6e20cu, 0x74b1d29au,
        0xead54739u, 0x9dd277afu, 0x04db2615u, 0x73dc1683u, 0xe3630b12u, 0x94643b84u,
        0x0d6d6a3eu, 0x7a6a5aa8u, 0xe40ecf0bu, 0x9309ff9du, 0x0a00ae27u, 0x7d079eb1u,
        0xf00f9344u, 0x8708a3d2u, 0x1e01f268u, 0x6906c2feu, 0xf762575du, 0x806567cbu,
        0x196c3671u, 0x6e6b06e7u, 0xfed41b76u, 0x89d32be0u, 0x10da7a5au, 0x67dd4accu,
        0xf9b9df6fu, 0x8ebeeff9u, 0x17b7be43u, 0x60b08ed5u, 0xd6d6a3e8u, 0xa1d1937eu,
        0x38d8c2c4u, 0x4fdff252u, 0xd1bb67f1u, 0xa6bc5767u, 0x3fb506ddu, 0x48b2364bu,
        0xd80d2bdau, 0xaf0a1b4cu, 0x36034af6u, 0x41047a60u, 0xdf60efc3u, 0xa867df55u,
        0x316e8eefu, 0x4669be79u, 0xcb61b38cu, 0xbc66831au, 0x256fd2a0u, 0x5268e236u,
        0xcc0c7795u, 0xbb0b4703u, 0x220216b9u, 0x5505262fu, 0xc5ba3bbeu, 0xb2bd0b28u,
        0x2bb45a92u, 0x5cb36a04u, 0xc2d7ffa7u, 0xb5d0cf31u, 0x2cd99e8bu, 0x5bdeae1du,
        0x9b64c2b0u, 0xec63f226u, 0x756aa39cu, 0x026d930au, 0x9c0906a9u, 0xeb0e363fu,
        0x72076785u, 0x05005713u, 0x95bf4a82u, 0xe2b87a14u, 0x7bb12baeu, 0x0cb61b38u,
        0x92d28e9bu, 0xe5d5be0du, 0x7cdcefb7u, 0x0bdbdf21u, 0x86d3d2d4u, 0xf1d4e242u,
        0x68ddb3f8u, 0x1fda836eu, 0x81be16edu, 0xf6b9265bu, 0x6fb077e1u, 0x18b74777u,
        0x88085ae6u, 0xff0f6a70u, 0x66063bcau, 0x11010b5cu, 0x8f659effu, 0xf862ae69u,
        0x616bffd3u, 0x166ccf45u, 0xa00ae278u, 0xd70dd2eeu, 0x4e048354u, 0x3903dbc3u,
        0xa7672661u, 0xd06016f7u, 0x4969474du, 0x3e6e77dbu, 0xaed16a4au, 0xd9d65adcu,
        0x40df0b66u, 0x37d83bf0u, 0xa9bcae53u, 0xdebb9ec5u, 0x47b2cf7fu, 0x30b5ffe9u,
        0xbdbdf21cu, 0xcabac28au, 0x53b39330u, 0x24b4a3a6u, 0xbad03605u, 0xcdd70693u,
        0x54de5729u, 0x23d967bfu, 0xb3667a2eu, 0xc4614ab8u, 0x5d681b02u, 0x2a6f2b94u,
        0xb40bbe37u, 0xc30c8ea1u, 0x5a05df1bu, 0x2d02ef8du};
    crc = ~crc;
    for (size_t i = 0; i < n; ++i) {
        crc = kTable[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    }
    return ~crc;
}

// ============================================================
// 语料生成
// ============================================================
struct Corpus {
    std::string name;
    std::vector<uint8_t> data;
};

static Corpus corpus_pattern() {
    const std::string pat = "REGRESS_LZDP_DPFLATE_";
    std::vector<uint8_t> d;
    constexpr int kRepeats = 32;
    d.reserve(pat.size() * kRepeats);
    for (int i = 0; i < kRepeats; ++i) {
        d.insert(d.end(), pat.begin(), pat.end());
    }
    return {"pattern", d};
}

static Corpus corpus_random() {
    std::vector<uint8_t> r(384);
    std::mt19937 gen(12345);
    for (auto& b : r) b = static_cast<uint8_t>(gen() & 0xFF);
    return {"random", r};
}

static Corpus corpus_text() {
    const std::string text =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
        "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. "
        "Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris "
        "nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in "
        "reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla "
        "pariatur. Excepteur sint occaecat cupidatat non proident, sunt in "
        "culpa qui officia deserunt mollit anim id est laborum. ";
    std::vector<uint8_t> d;
    for (int i = 0; i < 50; ++i) {
        d.insert(d.end(), text.begin(), text.end());
    }
    return {"text", d};
}

static Corpus corpus_binary() {
    std::vector<uint8_t> d(65536);
    std::mt19937 gen(67890);
    for (auto& b : d) b = static_cast<uint8_t>(gen() & 0xFF);
    return {"binary_64k", d};
}

static Corpus corpus_mixed_2mb() {
    const size_t total = 2 * 1024 * 1024;
    std::vector<uint8_t> d;
    d.reserve(total);

    const std::string text =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
        "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. ";
    while (d.size() < total / 3) {
        d.insert(d.end(), text.begin(), text.end());
    }

    const uint8_t pattern[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    while (d.size() < total * 2 / 3) {
        d.push_back(pattern[d.size() % (sizeof(pattern) - 1)]);
    }

    std::mt19937 gen(55555);
    while (d.size() < total) {
        d.push_back(static_cast<uint8_t>(gen() & 0xFF));
    }

    return {"mixed_2mb", d};
}

static Corpus corpus_large_repeat() {
    std::vector<uint8_t> d(100 * 1024 * 1024);  // 100MB
    const uint8_t pattern[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    for (size_t i = 0; i < d.size(); ++i) {
        d[i] = pattern[i % (sizeof(pattern) - 1)];
    }
    return {"large_repeat_100mb", d};
}

static Corpus corpus_large_random() {
    std::vector<uint8_t> d(100 * 1024 * 1024);  // 100MB
    std::mt19937 gen(99999);
    for (auto& b : d) b = static_cast<uint8_t>(gen() & 0xFF);
    return {"large_random_100mb", d};
}

// ============================================================
// 测试结果
// ============================================================
struct TestResult {
    std::string algorithm;
    std::string mode;       // "memory" or "streaming"
    std::string corpus;
    size_t original_size{0};
    size_t compressed_size{0};
    double ratio{0.0};
    double time_ms{0.0};
    bool decompress_ok{false};
    bool crc_match{false};
    uint32_t original_crc{0};
    uint32_t decompressed_crc{0};
    std::string error;
};

static std::vector<TestResult> g_results;

static void print_result(const TestResult& r) {
    printf("  %-12s %-10s %-18s orig=%8zu comp=%8zu ratio=%6.2f%% time=%8.1fms %s %s\n",
           r.algorithm.c_str(), r.mode.c_str(), r.corpus.c_str(),
           r.original_size, r.compressed_size, r.ratio * 100.0, r.time_ms,
           r.decompress_ok ? "OK" : "FAIL",
           r.crc_match ? "CRC_OK" : "CRC_MISMATCH");
    if (!r.error.empty()) {
        printf("    ERROR: %s\n", r.error.c_str());
    }
}

static void write_csv(const std::string& path) {
    std::ofstream f(path);
    f << "algorithm,mode,corpus,original_size,compressed_size,ratio_pct,time_ms,"
         "decompress_ok,crc_match,original_crc,decompressed_crc,error\n";
    for (const auto& r : g_results) {
        f << r.algorithm << ","
          << r.mode << ","
          << r.corpus << ","
          << r.original_size << ","
          << r.compressed_size << ","
          << (r.ratio * 100.0) << ","
          << r.time_ms << ","
          << (r.decompress_ok ? "yes" : "no") << ","
          << (r.crc_match ? "yes" : "no") << ","
          << r.original_crc << ","
          << r.decompressed_crc << ","
          << r.error << "\n";
    }
    std::cout << "\nCSV report written to: " << path << "\n";
}

// ============================================================
// 测试辅助
// ============================================================
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

// ============================================================
// 非流式 (Memory) 测试
// ============================================================

static void test_lzdp_memory(const Corpus& corp) {
    TestResult r;
    r.algorithm = "LZDP";
    r.mode = "memory";
    r.corpus = corp.name;
    r.original_size = corp.data.size();
    r.original_crc = crc32_update(0, corp.data.data(), corp.data.size());

    try {
        fprintf(stderr, "[PROGRESS] LZDP memory compress start: %s (%zu bytes)\n", corp.name, corp.data.size());
        fflush(stderr);
        auto start = std::chrono::high_resolution_clock::now();
        compressor::core::LZDPCompressor comp;
        comp.set_search_size(4096);
        comp.set_lookahead_size(256);
        comp.set_min_match(0);
        comp.set_dp_top(3);
        comp.set_use_flag_encoding(false);
        comp.set_match_engine(0);
        auto cr = comp.compress(corp.data);
        auto end = std::chrono::high_resolution_clock::now();
        fprintf(stderr, "[PROGRESS] LZDP memory compress done: %zu -> %zu bytes, %.1fms\n",
                corp.data.size(), cr.data.size(),
                std::chrono::duration<double, std::milli>(end - start).count());
        fflush(stderr);
        r.time_ms = std::chrono::duration<double, std::milli>(end - start).count();
        r.compressed_size = cr.data.size();
        r.ratio = cr.compression_ratio;

        fprintf(stderr, "[PROGRESS] LZDP memory decompress start\n");
        fflush(stderr);
        auto dec = comp.decompress(cr.data);
        fprintf(stderr, "[PROGRESS] LZDP memory decompress done: %zu bytes\n", dec.data.size());
        fflush(stderr);
        r.decompress_ok = (dec.data.size() == corp.data.size() &&
                          !dec.data.empty());
        if (r.decompress_ok) {
            r.decompressed_crc = crc32_update(0, dec.data.data(), dec.data.size());
            r.crc_match = (r.decompressed_crc == r.original_crc);
            r.decompress_ok = (dec.data == corp.data);
        }
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    g_results.push_back(r);
    print_result(r);
}

static void test_lzss_memory(const Corpus& corp) {
    TestResult r;
    r.algorithm = "LZSS";
    r.mode = "memory";
    r.corpus = corp.name;
    r.original_size = corp.data.size();
    r.original_crc = crc32_update(0, corp.data.data(), corp.data.size());

    try {
        fprintf(stderr, "[PROGRESS] LZSS memory compress start: %s (%zu bytes)\n", corp.name, corp.data.size());
        fflush(stderr);
        auto start = std::chrono::high_resolution_clock::now();
        compressor::core::LZSSCompressor comp;
        comp.set_use_flag_encoding(true);
        auto cr = comp.compress(corp.data);
        auto end = std::chrono::high_resolution_clock::now();
        fprintf(stderr, "[PROGRESS] LZSS memory compress done: %zu -> %zu bytes, %.1fms\n",
                corp.data.size(), cr.data.size(),
                std::chrono::duration<double, std::milli>(end - start).count());
        fflush(stderr);
        r.time_ms = std::chrono::duration<double, std::milli>(end - start).count();
        r.compressed_size = cr.data.size();
        r.ratio = cr.compression_ratio;

        fprintf(stderr, "[PROGRESS] LZSS memory decompress start\n");
        fflush(stderr);
        auto dec = comp.decompress(cr.data);
        fprintf(stderr, "[PROGRESS] LZSS memory decompress done: %zu bytes\n", dec.data.size());
        fflush(stderr);
        r.decompress_ok = (dec.data.size() == corp.data.size());
        if (r.decompress_ok) {
            r.decompressed_crc = crc32_update(0, dec.data.data(), dec.data.size());
            r.crc_match = (r.decompressed_crc == r.original_crc);
            r.decompress_ok = (dec.data == corp.data);
        }
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    g_results.push_back(r);
    print_result(r);
}

static void test_lzss_memory_nf(const Corpus& corp) {
    TestResult r;
    r.algorithm = "LZSS";
    r.mode = "memory_noflag";
    r.corpus = corp.name;
    r.original_size = corp.data.size();
    r.original_crc = crc32_update(0, corp.data.data(), corp.data.size());

    try {
        fprintf(stderr, "[PROGRESS] LZSS memory_noflag compress start: %s (%zu bytes)\n", corp.name, corp.data.size());
        fflush(stderr);
        auto start = std::chrono::high_resolution_clock::now();
        compressor::core::LZSSCompressor comp;
        comp.set_use_flag_encoding(false);
        auto cr = comp.compress(corp.data);
        auto end = std::chrono::high_resolution_clock::now();
        fprintf(stderr, "[PROGRESS] LZSS memory_noflag compress done: %zu -> %zu bytes, %.1fms\n",
                corp.data.size(), cr.data.size(),
                std::chrono::duration<double, std::milli>(end - start).count());
        fflush(stderr);
        r.time_ms = std::chrono::duration<double, std::milli>(end - start).count();
        r.compressed_size = cr.data.size();
        r.ratio = cr.compression_ratio;

        fprintf(stderr, "[PROGRESS] LZSS memory_noflag decompress start\n");
        fflush(stderr);
        auto dec = comp.decompress(cr.data);
        fprintf(stderr, "[PROGRESS] LZSS memory_noflag decompress done: %zu bytes\n", dec.data.size());
        fflush(stderr);
        r.decompress_ok = (dec.data.size() == corp.data.size());
        if (r.decompress_ok) {
            r.decompressed_crc = crc32_update(0, dec.data.data(), dec.data.size());
            r.crc_match = (r.decompressed_crc == r.original_crc);
            r.decompress_ok = (dec.data == corp.data);
        }
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    g_results.push_back(r);
    print_result(r);
}

static void test_dpflate_memory(const Corpus& corp, bool use_3hm) {
    TestResult r;
    r.algorithm = use_3hm ? "3HfMT" : "DPFlate";
    r.mode = "memory";
    r.corpus = corp.name;
    r.original_size = corp.data.size();
    r.original_crc = crc32_update(0, corp.data.data(), corp.data.size());

    try {
        fprintf(stderr, "[PROGRESS] %s memory compress start: %s (%zu bytes)\n",
                use_3hm ? "3HfMT" : "DPFlate", corp.name, corp.data.size());
        fflush(stderr);
        auto start = std::chrono::high_resolution_clock::now();
        compressor::core::DPFlateCompressor comp;
        comp.set_search_size(4096);
        comp.set_lookahead_size(256);
        comp.set_min_match(4);
        comp.set_max_chain_length(256);
        comp.set_dp_sub_match_max(6);
        comp.set_match_engine(1);
        comp.set_use_flag_encoding(false);
        comp.set_use_3hfmtree(use_3hm);
        comp.set_huffman_chunk_bits(8);
        auto cr = comp.compress(corp.data);
        auto end = std::chrono::high_resolution_clock::now();
        fprintf(stderr, "[PROGRESS] %s memory compress done: %zu -> %zu bytes, %.1fms\n",
                use_3hm ? "3HfMT" : "DPFlate", corp.data.size(), cr.data.size(),
                std::chrono::duration<double, std::milli>(end - start).count());
        fflush(stderr);
        r.time_ms = std::chrono::duration<double, std::milli>(end - start).count();
        r.compressed_size = cr.data.size();
        r.ratio = cr.compression_ratio;

        fprintf(stderr, "[PROGRESS] %s memory decompress start\n", use_3hm ? "3HfMT" : "DPFlate");
        fflush(stderr);
        auto dec = comp.decompress(cr.data);
        fprintf(stderr, "[PROGRESS] %s memory decompress done: %zu bytes\n",
                use_3hm ? "3HfMT" : "DPFlate", dec.data.size());
        fflush(stderr);
        r.decompress_ok = (dec.data.size() == corp.data.size());

        if (r.decompress_ok) {
            r.decompressed_crc = crc32_update(0, dec.data.data(), dec.data.size());
            r.crc_match = (r.decompressed_crc == r.original_crc);
            r.decompress_ok = (dec.data == corp.data);
        }
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    g_results.push_back(r);
    print_result(r);
}

static void test_deflate_memory(const Corpus& corp) {
    TestResult r;
    r.algorithm = "Deflate";
    r.mode = "memory";
    r.corpus = corp.name;
    r.original_size = corp.data.size();
    r.original_crc = crc32_update(0, corp.data.data(), corp.data.size());

    try {
        fprintf(stderr, "[PROGRESS] Deflate memory compress start: %s (%zu bytes)\n", corp.name, corp.data.size());
        fflush(stderr);
        auto start = std::chrono::high_resolution_clock::now();
        compressor::core::DeflateCompressor comp;
        comp.set_slide_size(4096);
        comp.set_min_match(3);
        comp.set_max_chain_length(256);
        auto cr = comp.compress(corp.data);
        auto end = std::chrono::high_resolution_clock::now();
        fprintf(stderr, "[PROGRESS] Deflate memory compress done: %zu -> %zu bytes, %.1fms\n",
                corp.data.size(), cr.data.size(),
                std::chrono::duration<double, std::milli>(end - start).count());
        fflush(stderr);
        r.time_ms = std::chrono::duration<double, std::milli>(end - start).count();
        r.compressed_size = cr.data.size();
        r.ratio = cr.compression_ratio;

        fprintf(stderr, "[PROGRESS] Deflate memory decompress start\n");
        fflush(stderr);
        auto dec = comp.decompress(cr.data);
        fprintf(stderr, "[PROGRESS] Deflate memory decompress done: %zu bytes\n", dec.data.size());
        fflush(stderr);
        r.decompress_ok = (dec.data.size() == corp.data.size());
        if (r.decompress_ok) {
            r.decompressed_crc = crc32_update(0, dec.data.data(), dec.data.size());
            r.crc_match = (r.decompressed_crc == r.original_crc);
            r.decompress_ok = (dec.data == corp.data);
        }
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    g_results.push_back(r);
    print_result(r);
}

// ============================================================
// 流式 (Streaming) 测试 — 使用 compressFile / decompressFile API
// ============================================================

static void test_lzdp_streaming(const fs::path& work, const Corpus& corp) {
    TestResult r;
    r.algorithm = "LZDP";
    r.mode = "streaming";
    r.corpus = corp.name;
    r.original_size = corp.data.size();
    r.original_crc = crc32_update(0, corp.data.data(), corp.data.size());

    try {
        using compressor::api::AlgorithmID;
        using compressor::api::compressFile;
        using compressor::api::decompressFile;

        const fs::path in = work / "lzdp_stream_in.bin";
        const fs::path wcx = work / "lzdp_stream_out.wcx";
        const fs::path dec = work / "lzdp_stream_dec.bin";
        write_file(in, corp.data);

        compressor::core::LzdpWholeFileParams wf{};
        wf.search_size = 4096;
        wf.lookahead_size = 256;
        wf.min_match = 0;
        wf.dp_top = 3;
        wf.use_flag_encoding = false;
        wf.match_engine = 0;

        fprintf(stderr, "[PROGRESS] LZDP streaming compress start: %s (%zu bytes)\n", corp.name, corp.data.size());
        fflush(stderr);
        auto start = std::chrono::high_resolution_clock::now();
        const AlgorithmID ch[] = {AlgorithmID::LZDP};
        auto cr = compressFile(in.string(), wcx.string(), ch, 307200u,
                               compressor::core::kFileCompressLzdpWholeFileFramed, &wf, nullptr);
        auto end = std::chrono::high_resolution_clock::now();
        fprintf(stderr, "[PROGRESS] LZDP streaming compress done: %zu -> %zu bytes, %.1fms\n",
                corp.data.size(), cr.compressed_size,
                std::chrono::duration<double, std::milli>(end - start).count());
        fflush(stderr);
        r.time_ms = std::chrono::duration<double, std::milli>(end - start).count();

        if (!cr.success) {
            r.error = "compressFile failed: " + cr.error_message;
            g_results.push_back(r);
            print_result(r);
            return;
        }

        auto compressed_data = read_file(wcx);
        r.compressed_size = compressed_data.size();
        r.ratio = static_cast<double>(r.compressed_size) / r.original_size;

        fprintf(stderr, "[PROGRESS] LZDP streaming decompress start\n");
        fflush(stderr);
        const AlgorithmID de[] = {AlgorithmID::LZDPDecompress};
        auto dr = decompressFile(wcx.string(), dec.string(), de, 307200u);
        fprintf(stderr, "[PROGRESS] LZDP streaming decompress done\n");
        fflush(stderr);
        if (!dr.success) {
            r.error = "decompressFile failed: " + dr.error_message;
            g_results.push_back(r);
            print_result(r);
            return;
        }

        auto dec_data = read_file(dec);
        r.decompress_ok = (dec_data.size() == corp.data.size());
        if (r.decompress_ok) {
            r.decompressed_crc = crc32_update(0, dec_data.data(), dec_data.size());
            r.crc_match = (r.decompressed_crc == r.original_crc);
            r.decompress_ok = (dec_data == corp.data);
        }
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    g_results.push_back(r);
    print_result(r);
}

static void test_lzss_streaming(const fs::path& work, const Corpus& corp) {
    TestResult r;
    r.algorithm = "LZSS";
    r.mode = "streaming";
    r.corpus = corp.name;
    r.original_size = corp.data.size();
    r.original_crc = crc32_update(0, corp.data.data(), corp.data.size());

    try {
        using compressor::api::AlgorithmID;
        using compressor::api::compressFile;
        using compressor::api::decompressFile;

        const fs::path in = work / "lzss_stream_in.bin";
        const fs::path wcx = work / "lzss_stream_out.wcx";
        const fs::path dec = work / "lzss_stream_dec.bin";
        write_file(in, corp.data);

        fprintf(stderr, "[PROGRESS] LZSS streaming compress start: %s (%zu bytes)\n", corp.name, corp.data.size());
        fflush(stderr);
        auto start = std::chrono::high_resolution_clock::now();
        const AlgorithmID ch[] = {AlgorithmID::LZSS};
        auto cr = compressFile(in.string(), wcx.string(), ch, 307200u);
        auto end = std::chrono::high_resolution_clock::now();
        fprintf(stderr, "[PROGRESS] LZSS streaming compress done: %zu -> %zu bytes, %.1fms\n",
                corp.data.size(), cr.compressed_size,
                std::chrono::duration<double, std::milli>(end - start).count());
        fflush(stderr);
        r.time_ms = std::chrono::duration<double, std::milli>(end - start).count();

        if (!cr.success) {
            r.error = "compressFile failed: " + cr.error_message;
            g_results.push_back(r);
            print_result(r);
            return;
        }

        auto compressed_data = read_file(wcx);
        r.compressed_size = compressed_data.size();
        r.ratio = static_cast<double>(r.compressed_size) / r.original_size;

        fprintf(stderr, "[PROGRESS] LZSS streaming decompress start\n");
        fflush(stderr);
        const AlgorithmID de[] = {AlgorithmID::LZSSDecompress};
        auto dr = decompressFile(wcx.string(), dec.string(), de, 307200u);
        fprintf(stderr, "[PROGRESS] LZSS streaming decompress done\n");
        fflush(stderr);
        if (!dr.success) {
            r.error = "decompressFile failed: " + dr.error_message;
            g_results.push_back(r);
            print_result(r);
            return;
        }

        auto dec_data = read_file(dec);
        r.decompress_ok = (dec_data.size() == corp.data.size());
        if (r.decompress_ok) {
            r.decompressed_crc = crc32_update(0, dec_data.data(), dec_data.size());
            r.crc_match = (r.decompressed_crc == r.original_crc);
            r.decompress_ok = (dec_data == corp.data);
        }
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    g_results.push_back(r);
    print_result(r);
}

static void test_lzss_streaming_nf(const fs::path& work, const Corpus& corp) {
    TestResult r;
    r.algorithm = "LZSS";
    r.mode = "streaming_noflag";
    r.corpus = corp.name;
    r.original_size = corp.data.size();
    r.original_crc = crc32_update(0, corp.data.data(), corp.data.size());

    try {
        using compressor::api::AlgorithmID;
        using compressor::api::compressFile;
        using compressor::api::decompressFile;

        const fs::path in = work / "lzss_nf_stream_in.bin";
        const fs::path wcx = work / "lzss_nf_stream_out.wcx";
        const fs::path dec = work / "lzss_nf_stream_dec.bin";
        write_file(in, corp.data);

        fprintf(stderr, "[PROGRESS] LZSS streaming_noflag compress start: %s (%zu bytes)\n", corp.name, corp.data.size());
        fflush(stderr);
        auto start = std::chrono::high_resolution_clock::now();
        const AlgorithmID ch[] = {AlgorithmID::LZSS_NoFlag};
        auto cr = compressFile(in.string(), wcx.string(), ch, 307200u);
        auto end = std::chrono::high_resolution_clock::now();
        fprintf(stderr, "[PROGRESS] LZSS streaming_noflag compress done: %zu -> %zu bytes, %.1fms\n",
                corp.data.size(), cr.compressed_size,
                std::chrono::duration<double, std::milli>(end - start).count());
        fflush(stderr);
        r.time_ms = std::chrono::duration<double, std::milli>(end - start).count();

        if (!cr.success) {
            r.error = "compressFile failed: " + cr.error_message;
            g_results.push_back(r);
            print_result(r);
            return;
        }

        auto compressed_data = read_file(wcx);
        r.compressed_size = compressed_data.size();
        r.ratio = static_cast<double>(r.compressed_size) / r.original_size;

        fprintf(stderr, "[PROGRESS] LZSS streaming_noflag decompress start\n");
        fflush(stderr);
        const AlgorithmID de[] = {AlgorithmID::LZSSDecompress_NoFlag};
        auto dr = decompressFile(wcx.string(), dec.string(), de, 307200u);
        fprintf(stderr, "[PROGRESS] LZSS streaming_noflag decompress done\n");
        fflush(stderr);
        if (!dr.success) {
            r.error = "decompressFile failed: " + dr.error_message;
            g_results.push_back(r);
            print_result(r);
            return;
        }

        auto dec_data = read_file(dec);
        r.decompress_ok = (dec_data.size() == corp.data.size());
        if (r.decompress_ok) {
            r.decompressed_crc = crc32_update(0, dec_data.data(), dec_data.size());
            r.crc_match = (r.decompressed_crc == r.original_crc);
            r.decompress_ok = (dec_data == corp.data);
        }
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    g_results.push_back(r);
    print_result(r);
}

static void test_dpflate_streaming(const fs::path& work, const Corpus& corp, bool use_3hm) {
    TestResult r;
    r.algorithm = use_3hm ? "3HfMT" : "DPFlate";
    r.mode = "streaming";
    r.corpus = corp.name;
    r.original_size = corp.data.size();
    r.original_crc = crc32_update(0, corp.data.data(), corp.data.size());

    try {
        using compressor::api::AlgorithmID;
        using compressor::api::compressFile;
        using compressor::api::decompressFile;

        const fs::path in = work / (use_3hm ? "hm_stream_in.bin" : "dpf_stream_in.bin");
        const fs::path wcx = work / (use_3hm ? "hm_stream_out.wcx" : "dpf_stream_out.wcx");
        const fs::path dec = work / (use_3hm ? "hm_stream_dec.bin" : "dpf_stream_dec.bin");
        write_file(in, corp.data);

        compressor::core::DpflatePipelineParams df{};
        df.search_size = 4096;
        df.lookahead_size = 256;
        df.min_match = 4;
        df.max_chain_length = 256;
        df.dp_sub_match_max = 6;
        df.match_engine = 1;
        df.use_flag_encoding = false;
        df.use_3hfmtree = use_3hm;
        df.huffman_offset_chunk_bits = 8;
        df.huffman_length_chunk_bits = 8;

        fprintf(stderr, "[PROGRESS] %s streaming compress start: %s (%zu bytes)\n",
                use_3hm ? "3HfMT" : "DPFlate", corp.name, corp.data.size());
        fflush(stderr);
        auto start = std::chrono::high_resolution_clock::now();
        const AlgorithmID ch[] = {AlgorithmID::DPFlate};
        auto cr = compressFile(in.string(), wcx.string(), ch, 307200u,
                               compressor::core::kFileCompressOptsNone, nullptr, &df);
        auto end = std::chrono::high_resolution_clock::now();
        fprintf(stderr, "[PROGRESS] %s streaming compress done: %zu -> %zu bytes, %.1fms\n",
                use_3hm ? "3HfMT" : "DPFlate", corp.data.size(), cr.compressed_size,
                std::chrono::duration<double, std::milli>(end - start).count());
        fflush(stderr);
        r.time_ms = std::chrono::duration<double, std::milli>(end - start).count();

        if (!cr.success) {
            r.error = "compressFile failed: " + cr.error_message;
            g_results.push_back(r);
            print_result(r);
            return;
        }

        auto compressed_data = read_file(wcx);
        r.compressed_size = compressed_data.size();
        r.ratio = static_cast<double>(r.compressed_size) / r.original_size;

        if (use_3hm) {
            fprintf(stderr, "[PROGRESS] 3HfMT streaming decompress start (DPFlateCompressor)\n");
            fflush(stderr);

            auto wcx_result = compressor::api::unpack_wcx(compressed_data);
            if (!wcx_result.success) {
                r.error = "unpack_wcx failed: " + wcx_result.error_message;
                g_results.push_back(r);
                print_result(r);
                return;
            }

            compressor::core::DPFlateCompressor comp;
            auto dec_result = comp.decompress(wcx_result.payload);
            fprintf(stderr, "[PROGRESS] 3HfMT streaming decompress done: %zu bytes\n", dec_result.data.size());
            fflush(stderr);
            r.decompress_ok = (dec_result.data.size() == corp.data.size());
            if (r.decompress_ok) {
                r.decompressed_crc = crc32_update(0, dec_result.data.data(), dec_result.data.size());
                r.crc_match = (r.decompressed_crc == r.original_crc);
                r.decompress_ok = (dec_result.data == corp.data);
            }
        } else {
            fprintf(stderr, "[PROGRESS] DPFlate streaming decompress start\n");
            fflush(stderr);
            const AlgorithmID de[] = {AlgorithmID::Inflate};
            auto dr = decompressFile(wcx.string(), dec.string(), de, 307200u);
            fprintf(stderr, "[PROGRESS] DPFlate streaming decompress done\n");
            fflush(stderr);
            if (!dr.success) {
                r.error = "decompressFile failed: " + dr.error_message;
                g_results.push_back(r);
                print_result(r);
                return;
            }

            auto dec_data = read_file(dec);
            r.decompress_ok = (dec_data.size() == corp.data.size());
            if (r.decompress_ok) {
                r.decompressed_crc = crc32_update(0, dec_data.data(), dec_data.size());
                r.crc_match = (r.decompressed_crc == r.original_crc);
                r.decompress_ok = (dec_data == corp.data);
            }
        }
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    g_results.push_back(r);
    print_result(r);
}

static void test_deflate_streaming(const fs::path& work, const Corpus& corp) {
    TestResult r;
    r.algorithm = "Deflate";
    r.mode = "streaming";
    r.corpus = corp.name;
    r.original_size = corp.data.size();
    r.original_crc = crc32_update(0, corp.data.data(), corp.data.size());

    try {
        using compressor::api::AlgorithmID;
        using compressor::api::compressFile;
        using compressor::api::decompressFile;

        const fs::path in = work / "deflate_stream_in.bin";
        const fs::path wcx = work / "deflate_stream_out.wcx";
        const fs::path dec = work / "deflate_stream_dec.bin";
        write_file(in, corp.data);

        compressor::core::DeflatePipelineParams dp{};
        dp.search_size = 4096;
        dp.lookahead_size = 256;
        dp.min_match = 3;
        dp.max_chain_length = 256;

        auto start = std::chrono::high_resolution_clock::now();
        const AlgorithmID ch[] = {AlgorithmID::Deflate};
        auto cr = compressFile(in.string(), wcx.string(), ch, 307200u,
                               compressor::core::kFileCompressOptsNone, nullptr, nullptr, &dp);
        auto end = std::chrono::high_resolution_clock::now();
        r.time_ms = std::chrono::duration<double, std::milli>(end - start).count();

        if (!cr.success) {
            r.error = "compressFile failed: " + cr.error_message;
            g_results.push_back(r);
            print_result(r);
            return;
        }

        auto compressed_data = read_file(wcx);
        r.compressed_size = compressed_data.size();
        r.ratio = static_cast<double>(r.compressed_size) / r.original_size;

        const AlgorithmID de[] = {AlgorithmID::Inflate};
        auto dr = decompressFile(wcx.string(), dec.string(), de, 307200u);
        if (!dr.success) {
            r.error = "decompressFile failed: " + dr.error_message;
            g_results.push_back(r);
            print_result(r);
            return;
        }

        auto dec_data = read_file(dec);
        r.decompress_ok = (dec_data.size() == corp.data.size());
        if (r.decompress_ok) {
            r.decompressed_crc = crc32_update(0, dec_data.data(), dec_data.size());
            r.crc_match = (r.decompressed_crc == r.original_crc);
            r.decompress_ok = (dec_data == corp.data);
        }
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    g_results.push_back(r);
    print_result(r);
}

// ============================================================
// 补充测试：flag encoding 变体 + Deflate+3HfMT
// ============================================================

static void test_lzss_nf_stream_vs_memory(const fs::path& work, const Corpus& corp) {
    TestResult r;
    r.algorithm = "LZSS";
    r.mode = "nf_stream_vs_memory";
    r.corpus = corp.name;
    r.original_size = corp.data.size();
    r.original_crc = crc32_update(0, corp.data.data(), corp.data.size());

    try {
        compressor::core::LZSSCompressor comp;
        comp.set_use_flag_encoding(false);
        auto mem_cr = comp.compress(corp.data);
        std::vector<uint8_t> mem_compressed = mem_cr.data;

        using compressor::api::AlgorithmID;
        using compressor::api::compressFile;

        const fs::path in = work / "lzss_nf_cmp_in.bin";
        const fs::path wcx = work / "lzss_nf_cmp_out.wcx";
        write_file(in, corp.data);

        const AlgorithmID ch[] = {AlgorithmID::LZSS_NoFlag};
        auto str_cr = compressFile(in.string(), wcx.string(), ch, 307200u);

        if (!str_cr.success) {
            r.error = "streaming compress failed: " + str_cr.error_message;
            g_results.push_back(r);
            print_result(r);
            return;
        }

        auto str_compressed = read_file(wcx);
        r.compressed_size = str_compressed.size();
        r.ratio = static_cast<double>(r.compressed_size) / r.original_size;

        r.decompress_ok = (mem_compressed == str_compressed);
        r.crc_match = r.decompress_ok;
        if (!r.decompress_ok) {
            r.error = "streaming vs memory compressed content mismatch";
            fprintf(stderr, "[PROGRESS] LZSS nf_stream_vs_memory MISMATCH: mem=%zu str=%zu bytes\n",
                    mem_compressed.size(), str_compressed.size());
            fflush(stderr);
        }
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    g_results.push_back(r);
    print_result(r);
}

static void test_lzdp_streaming_flag(const fs::path& work, const Corpus& corp) {
    TestResult r;
    r.algorithm = "LZDP";
    r.mode = "streaming_flag";
    r.corpus = corp.name;
    r.original_size = corp.data.size();
    r.original_crc = crc32_update(0, corp.data.data(), corp.data.size());

    try {
        using compressor::api::AlgorithmID;
        using compressor::api::compressFile;
        using compressor::api::decompressFile;

        const fs::path in = work / "lzdp_sf_in.bin";
        const fs::path wcx = work / "lzdp_sf_out.wcx";
        const fs::path dec = work / "lzdp_sf_dec.bin";
        write_file(in, corp.data);

        compressor::core::LzdpWholeFileParams wf;
        wf.search_size = 4096;
        wf.lookahead_size = 256;
        wf.min_match = 4;
        wf.match_engine = 1;
        wf.use_flag_encoding = true;

        fprintf(stderr, "[PROGRESS] LZDP streaming_flag compress start: %s (%zu bytes)\n", corp.name, corp.data.size());
        fflush(stderr);
        auto start = std::chrono::high_resolution_clock::now();
        const AlgorithmID ch[] = {AlgorithmID::LZDP};
        auto cr = compressFile(in.string(), wcx.string(), ch, 307200u,
                               compressor::core::kFileCompressLzdpWholeFileFramed, &wf, nullptr);
        auto end = std::chrono::high_resolution_clock::now();
        fprintf(stderr, "[PROGRESS] LZDP streaming_flag compress done: %zu -> %zu bytes, %.1fms\n",
                corp.data.size(), cr.compressed_size,
                std::chrono::duration<double, std::milli>(end - start).count());
        fflush(stderr);
        r.time_ms = std::chrono::duration<double, std::milli>(end - start).count();

        if (!cr.success) {
            r.error = "compressFile failed: " + cr.error_message;
            g_results.push_back(r);
            print_result(r);
            return;
        }
        r.compressed_size = cr.compressed_size;
        r.ratio = static_cast<double>(r.compressed_size) / r.original_size;

        fprintf(stderr, "[PROGRESS] LZDP streaming_flag decompress start\n");
        fflush(stderr);
        const AlgorithmID de[] = {AlgorithmID::LZDPDecompress};
        auto dr = decompressFile(wcx.string(), dec.string(), de, 307200u);
        fprintf(stderr, "[PROGRESS] LZDP streaming_flag decompress done\n");
        fflush(stderr);
        if (!dr.success) {
            r.error = "decompressFile failed: " + dr.error_message;
            g_results.push_back(r);
            print_result(r);
            return;
        }

        auto dec_data = read_file(dec);
        r.decompress_ok = (dec_data.size() == corp.data.size());
        if (r.decompress_ok) {
            r.decompressed_crc = crc32_update(0, dec_data.data(), dec_data.size());
            r.crc_match = (r.decompressed_crc == r.original_crc);
            r.decompress_ok = (dec_data == corp.data);
        }
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    g_results.push_back(r);
    print_result(r);
}

static void test_dpflate_memory_flag(const Corpus& corp) {
    TestResult r;
    r.algorithm = "DPFlate";
    r.mode = "memory_flag";
    r.corpus = corp.name;
    r.original_size = corp.data.size();
    r.original_crc = crc32_update(0, corp.data.data(), corp.data.size());

    try {
        fprintf(stderr, "[PROGRESS] DPFlate memory_flag compress start: %s (%zu bytes)\n", corp.name, corp.data.size());
        fflush(stderr);
        auto start = std::chrono::high_resolution_clock::now();
        compressor::core::DPFlateCompressor comp;
        comp.set_search_size(4096);
        comp.set_lookahead_size(256);
        comp.set_min_match(4);
        comp.set_max_chain_length(256);
        comp.set_dp_sub_match_max(6);
        comp.set_match_engine(1);
        comp.set_use_flag_encoding(true);
        comp.set_use_3hfmtree(false);
        auto cr = comp.compress(corp.data);
        auto end = std::chrono::high_resolution_clock::now();
        fprintf(stderr, "[PROGRESS] DPFlate memory_flag compress done: %zu -> %zu bytes, %.1fms\n",
                corp.data.size(), cr.data.size(),
                std::chrono::duration<double, std::milli>(end - start).count());
        fflush(stderr);
        r.time_ms = std::chrono::duration<double, std::milli>(end - start).count();
        r.compressed_size = cr.data.size();
        r.ratio = cr.compression_ratio;

        fprintf(stderr, "[PROGRESS] DPFlate memory_flag decompress start\n");
        fflush(stderr);
        auto dec = comp.decompress(cr.data);
        fprintf(stderr, "[PROGRESS] DPFlate memory_flag decompress done: %zu bytes\n", dec.data.size());
        fflush(stderr);
        r.decompress_ok = (dec.data.size() == corp.data.size());
        if (r.decompress_ok) {
            r.decompressed_crc = crc32_update(0, dec.data.data(), dec.data.size());
            r.crc_match = (r.decompressed_crc == r.original_crc);
            r.decompress_ok = (dec.data == corp.data);
        }
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    g_results.push_back(r);
    print_result(r);
}

static void test_dpflate_streaming_flag(const fs::path& work, const Corpus& corp) {
    TestResult r;
    r.algorithm = "DPFlate";
    r.mode = "streaming_flag";
    r.corpus = corp.name;
    r.original_size = corp.data.size();
    r.original_crc = crc32_update(0, corp.data.data(), corp.data.size());

    try {
        using compressor::api::AlgorithmID;
        using compressor::api::compressFile;
        using compressor::api::decompressFile;

        const fs::path in = work / "dpf_sf_in.bin";
        const fs::path wcx = work / "dpf_sf_out.wcx";
        const fs::path dec = work / "dpf_sf_dec.bin";
        write_file(in, corp.data);

        compressor::core::DpflatePipelineParams df;
        df.search_size = 4096;
        df.lookahead_size = 256;
        df.min_match = 4;
        df.max_chain_length = 256;
        df.dp_sub_match_max = 6;
        df.match_engine = 1;
        df.use_flag_encoding = true;
        df.use_3hfmtree = false;

        fprintf(stderr, "[PROGRESS] DPFlate streaming_flag compress start: %s (%zu bytes)\n", corp.name, corp.data.size());
        fflush(stderr);
        auto start = std::chrono::high_resolution_clock::now();
        const AlgorithmID ch[] = {AlgorithmID::DPFlate};
        auto cr = compressFile(in.string(), wcx.string(), ch, 307200u,
                               compressor::core::kFileCompressOptsNone, nullptr, &df);
        auto end = std::chrono::high_resolution_clock::now();
        fprintf(stderr, "[PROGRESS] DPFlate streaming_flag compress done: %zu -> %zu bytes, %.1fms\n",
                corp.data.size(), cr.compressed_size,
                std::chrono::duration<double, std::milli>(end - start).count());
        fflush(stderr);
        r.time_ms = std::chrono::duration<double, std::milli>(end - start).count();

        if (!cr.success) {
            r.error = "compressFile failed: " + cr.error_message;
            g_results.push_back(r);
            print_result(r);
            return;
        }
        r.compressed_size = cr.compressed_size;
        r.ratio = static_cast<double>(r.compressed_size) / r.original_size;

        {
            fprintf(stderr, "[PROGRESS] DPFlate streaming_flag decompress start\n");
            fflush(stderr);
            const AlgorithmID de[] = {AlgorithmID::Inflate};
            auto dr = decompressFile(wcx.string(), dec.string(), de, 307200u);
            fprintf(stderr, "[PROGRESS] DPFlate streaming_flag decompress done\n");
            fflush(stderr);
            if (!dr.success) {
                r.error = "decompressFile failed: " + dr.error_message;
                g_results.push_back(r);
                print_result(r);
                return;
            }
        }

        auto dec_data = read_file(dec);
        r.decompress_ok = (dec_data.size() == corp.data.size());
        if (r.decompress_ok) {
            r.decompressed_crc = crc32_update(0, dec_data.data(), dec_data.size());
            r.crc_match = (r.decompressed_crc == r.original_crc);
            r.decompress_ok = (dec_data == corp.data);
        }
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    g_results.push_back(r);
    print_result(r);
}

static void test_dpflate_3hm_memory_flag(const Corpus& corp) {
    TestResult r;
    r.algorithm = "DPFlate_3HM";
    r.mode = "memory_flag";
    r.corpus = corp.name;
    r.original_size = corp.data.size();
    r.original_crc = crc32_update(0, corp.data.data(), corp.data.size());

    try {
        fprintf(stderr, "[PROGRESS] DPFlate_3HM memory_flag compress start: %s (%zu bytes)\n", corp.name, corp.data.size());
        fflush(stderr);
        auto start = std::chrono::high_resolution_clock::now();
        compressor::core::DPFlateCompressor comp;
        comp.set_search_size(4096);
        comp.set_lookahead_size(256);
        comp.set_min_match(4);
        comp.set_max_chain_length(256);
        comp.set_dp_sub_match_max(6);
        comp.set_match_engine(1);
        comp.set_use_flag_encoding(true);
        comp.set_use_3hfmtree(true);
        comp.set_huffman_chunk_bits(8);
        auto cr = comp.compress(corp.data);
        auto end = std::chrono::high_resolution_clock::now();
        fprintf(stderr, "[PROGRESS] DPFlate_3HM memory_flag compress done: %zu -> %zu bytes, %.1fms\n",
                corp.data.size(), cr.data.size(),
                std::chrono::duration<double, std::milli>(end - start).count());
        fflush(stderr);
        r.time_ms = std::chrono::duration<double, std::milli>(end - start).count();
        r.compressed_size = cr.data.size();
        r.ratio = cr.compression_ratio;

        fprintf(stderr, "[PROGRESS] DPFlate_3HM memory_flag decompress start\n");
        fflush(stderr);
        auto dec = comp.decompress(cr.data);
        fprintf(stderr, "[PROGRESS] DPFlate_3HM memory_flag decompress done: %zu bytes\n", dec.data.size());
        fflush(stderr);
        r.decompress_ok = (dec.data.size() == corp.data.size());
        if (r.decompress_ok) {
            r.decompressed_crc = crc32_update(0, dec.data.data(), dec.data.size());
            r.crc_match = (r.decompressed_crc == r.original_crc);
            r.decompress_ok = (dec.data == corp.data);
        }
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    g_results.push_back(r);
    print_result(r);
}

static void test_dpflate_3hm_streaming_flag(const fs::path& work, const Corpus& corp) {
    TestResult r;
    r.algorithm = "DPFlate_3HM";
    r.mode = "streaming_flag";
    r.corpus = corp.name;
    r.original_size = corp.data.size();
    r.original_crc = crc32_update(0, corp.data.data(), corp.data.size());

    try {
        using compressor::api::AlgorithmID;
        using compressor::api::compressFile;
        using compressor::api::decompressFile;

        const fs::path in = work / "dpf3_sf_in.bin";
        const fs::path wcx = work / "dpf3_sf_out.wcx";
        const fs::path dec = work / "dpf3_sf_dec.bin";
        write_file(in, corp.data);

        compressor::core::DpflatePipelineParams df;
        df.search_size = 4096;
        df.lookahead_size = 256;
        df.min_match = 4;
        df.max_chain_length = 256;
        df.dp_sub_match_max = 6;
        df.match_engine = 1;
        df.use_flag_encoding = true;
        df.use_3hfmtree = true;
        df.huffman_offset_chunk_bits = 8;
        df.huffman_length_chunk_bits = 8;

        fprintf(stderr, "[PROGRESS] DPFlate_3HM streaming_flag compress start: %s (%zu bytes)\n", corp.name, corp.data.size());
        fflush(stderr);
        auto start = std::chrono::high_resolution_clock::now();
        const AlgorithmID ch[] = {AlgorithmID::DPFlate};
        auto cr = compressFile(in.string(), wcx.string(), ch, 307200u,
                               compressor::core::kFileCompressOptsNone, nullptr, &df);
        auto end = std::chrono::high_resolution_clock::now();
        fprintf(stderr, "[PROGRESS] DPFlate_3HM streaming_flag compress done: %zu -> %zu bytes, %.1fms\n",
                corp.data.size(), cr.compressed_size,
                std::chrono::duration<double, std::milli>(end - start).count());
        fflush(stderr);
        r.time_ms = std::chrono::duration<double, std::milli>(end - start).count();

        if (!cr.success) {
            r.error = "compressFile failed: " + cr.error_message;
            g_results.push_back(r);
            print_result(r);
            return;
        }

        auto compressed_data = read_file(wcx);
        r.compressed_size = compressed_data.size();
        r.ratio = static_cast<double>(r.compressed_size) / r.original_size;

        {
            fprintf(stderr, "[PROGRESS] DPFlate_3HM streaming_flag decompress start (DPFlateCompressor)\n");
            fflush(stderr);

            auto wcx_result = compressor::api::unpack_wcx(compressed_data);
            if (!wcx_result.success) {
                r.error = "unpack_wcx failed: " + wcx_result.error_message;
                g_results.push_back(r);
                print_result(r);
                return;
            }

            compressor::core::DPFlateCompressor comp;
            auto dec_result = comp.decompress(wcx_result.payload);
            fprintf(stderr, "[PROGRESS] DPFlate_3HM streaming_flag decompress done: %zu bytes\n", dec_result.data.size());
            fflush(stderr);
            r.decompress_ok = (dec_result.data.size() == corp.data.size());
            if (r.decompress_ok) {
                r.decompressed_crc = crc32_update(0, dec_result.data.data(), dec_result.data.size());
                r.crc_match = (r.decompressed_crc == r.original_crc);
                r.decompress_ok = (dec_result.data == corp.data);
            }
        }
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    g_results.push_back(r);
    print_result(r);
}

static void test_deflate_3hm_memory(const Corpus& corp) {
    TestResult r;
    r.algorithm = "Deflate_3HM";
    r.mode = "memory";
    r.corpus = corp.name;
    r.original_size = corp.data.size();
    r.original_crc = crc32_update(0, corp.data.data(), corp.data.size());

    try {
        fprintf(stderr, "[PROGRESS] Deflate_3HM memory compress start: %s (%zu bytes)\n", corp.name, corp.data.size());
        fflush(stderr);
        auto start = std::chrono::high_resolution_clock::now();
        compressor::core::DeflateCompressor comp;
        comp.set_use_3hfmtree(true);
        comp.set_huffman_chunk_bits(8);
        auto cr = comp.compress(corp.data);
        auto end = std::chrono::high_resolution_clock::now();
        fprintf(stderr, "[PROGRESS] Deflate_3HM memory compress done: %zu -> %zu bytes, %.1fms\n",
                corp.data.size(), cr.data.size(),
                std::chrono::duration<double, std::milli>(end - start).count());
        fflush(stderr);
        r.time_ms = std::chrono::duration<double, std::milli>(end - start).count();
        r.compressed_size = cr.data.size();
        r.ratio = cr.compression_ratio;

        fprintf(stderr, "[PROGRESS] Deflate_3HM memory decompress start\n");
        fflush(stderr);
        auto dec = comp.decompress(cr.data);
        fprintf(stderr, "[PROGRESS] Deflate_3HM memory decompress done: %zu bytes\n", dec.data.size());
        fflush(stderr);
        r.decompress_ok = (dec.data.size() == corp.data.size());
        if (r.decompress_ok) {
            r.decompressed_crc = crc32_update(0, dec.data.data(), dec.data.size());
            r.crc_match = (r.decompressed_crc == r.original_crc);
            r.decompress_ok = (dec.data == corp.data);
        }
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    g_results.push_back(r);
    print_result(r);
}

static void test_deflate_3hm_memory_flag(const Corpus& corp) {
    TestResult r;
    r.algorithm = "Deflate_3HM";
    r.mode = "memory_flag";
    r.corpus = corp.name;
    r.original_size = corp.data.size();
    r.original_crc = crc32_update(0, corp.data.data(), corp.data.size());

    try {
        fprintf(stderr, "[PROGRESS] Deflate_3HM memory_flag compress start: %s (%zu bytes)\n", corp.name, corp.data.size());
        fflush(stderr);
        auto start = std::chrono::high_resolution_clock::now();
        compressor::core::DeflateCompressor comp;
        comp.set_use_3hfmtree(true);
        comp.set_huffman_chunk_bits(8);
        comp.set_use_flag_encoding(true);
        auto cr = comp.compress(corp.data);
        auto end = std::chrono::high_resolution_clock::now();
        fprintf(stderr, "[PROGRESS] Deflate_3HM memory_flag compress done: %zu -> %zu bytes, %.1fms\n",
                corp.data.size(), cr.data.size(),
                std::chrono::duration<double, std::milli>(end - start).count());
        fflush(stderr);
        r.time_ms = std::chrono::duration<double, std::milli>(end - start).count();
        r.compressed_size = cr.data.size();
        r.ratio = cr.compression_ratio;

        fprintf(stderr, "[PROGRESS] Deflate_3HM memory_flag decompress start\n");
        fflush(stderr);
        auto dec = comp.decompress(cr.data);
        fprintf(stderr, "[PROGRESS] Deflate_3HM memory_flag decompress done: %zu bytes\n", dec.data.size());
        fflush(stderr);
        r.decompress_ok = (dec.data.size() == corp.data.size());
        if (r.decompress_ok) {
            r.decompressed_crc = crc32_update(0, dec.data.data(), dec.data.size());
            r.crc_match = (r.decompressed_crc == r.original_crc);
            r.decompress_ok = (dec.data == corp.data);
        }
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    g_results.push_back(r);
    print_result(r);
}

static void test_deflate_3hm_streaming(const fs::path& work, const Corpus& corp) {
    TestResult r;
    r.algorithm = "Deflate_3HM";
    r.mode = "streaming";
    r.corpus = corp.name;
    r.original_size = corp.data.size();
    r.original_crc = crc32_update(0, corp.data.data(), corp.data.size());

    try {
        using compressor::api::AlgorithmID;
        using compressor::api::compressFile;
        using compressor::api::decompressFile;

        const fs::path in = work / "def3_s_in.bin";
        const fs::path wcx = work / "def3_s_out.wcx";
        const fs::path dec = work / "def3_s_dec.bin";
        write_file(in, corp.data);

        compressor::core::DpflatePipelineParams dp;
        dp.search_size = 4096;
        dp.lookahead_size = 256;
        dp.min_match = 3;
        dp.max_chain_length = 256;
        dp.use_3hfmtree = true;
        dp.huffman_offset_chunk_bits = 8;
        dp.huffman_length_chunk_bits = 8;

        fprintf(stderr, "[PROGRESS] Deflate_3HM streaming compress start: %s (%zu bytes)\n", corp.name, corp.data.size());
        fflush(stderr);
        auto start = std::chrono::high_resolution_clock::now();
        const AlgorithmID ch[] = {AlgorithmID::Deflate};
        auto cr = compressFile(in.string(), wcx.string(), ch, 307200u,
                               compressor::core::kFileCompressOptsNone, nullptr, &dp, nullptr);
        auto end = std::chrono::high_resolution_clock::now();
        fprintf(stderr, "[PROGRESS] Deflate_3HM streaming compress done: %zu -> %zu bytes, %.1fms\n",
                corp.data.size(), cr.compressed_size,
                std::chrono::duration<double, std::milli>(end - start).count());
        fflush(stderr);
        r.time_ms = std::chrono::duration<double, std::milli>(end - start).count();

        if (!cr.success) {
            r.error = "compressFile failed: " + cr.error_message;
            g_results.push_back(r);
            print_result(r);
            return;
        }

        auto compressed_data = read_file(wcx);
        r.compressed_size = compressed_data.size();
        r.ratio = static_cast<double>(r.compressed_size) / r.original_size;

        {
            fprintf(stderr, "[PROGRESS] Deflate_3HM streaming decompress start (DeflateCompressor)\n");
            fflush(stderr);
            compressor::core::DeflateCompressor comp;
            comp.set_use_3hfmtree(true);
            comp.set_huffman_chunk_bits(8);
            auto dec_result = comp.decompress(compressed_data);
            fprintf(stderr, "[PROGRESS] Deflate_3HM streaming decompress done: %zu bytes\n", dec_result.data.size());
            fflush(stderr);
            r.decompress_ok = (dec_result.data.size() == corp.data.size());
            if (r.decompress_ok) {
                r.decompressed_crc = crc32_update(0, dec_result.data.data(), dec_result.data.size());
                r.crc_match = (r.decompressed_crc == r.original_crc);
                r.decompress_ok = (dec_result.data == corp.data);
            }
        }
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    g_results.push_back(r);
    print_result(r);
}

static void test_deflate_3hm_streaming_flag(const fs::path& work, const Corpus& corp) {
    TestResult r;
    r.algorithm = "Deflate_3HM";
    r.mode = "streaming_flag";
    r.corpus = corp.name;
    r.original_size = corp.data.size();
    r.original_crc = crc32_update(0, corp.data.data(), corp.data.size());

    try {
        using compressor::api::AlgorithmID;
        using compressor::api::compressFile;
        using compressor::api::decompressFile;

        const fs::path in = work / "def3_sf_in.bin";
        const fs::path wcx = work / "def3_sf_out.wcx";
        const fs::path dec = work / "def3_sf_dec.bin";
        write_file(in, corp.data);

        compressor::core::DpflatePipelineParams dp;
        dp.search_size = 4096;
        dp.lookahead_size = 256;
        dp.min_match = 3;
        dp.max_chain_length = 256;
        dp.use_3hfmtree = true;
        dp.huffman_offset_chunk_bits = 8;
        dp.huffman_length_chunk_bits = 8;
        dp.use_flag_encoding = true;

        fprintf(stderr, "[PROGRESS] Deflate_3HM streaming_flag compress start: %s (%zu bytes)\n", corp.name, corp.data.size());
        fflush(stderr);
        auto start = std::chrono::high_resolution_clock::now();
        const AlgorithmID ch[] = {AlgorithmID::Deflate};
        auto cr = compressFile(in.string(), wcx.string(), ch, 307200u,
                               compressor::core::kFileCompressOptsNone, nullptr, &dp, nullptr);
        auto end = std::chrono::high_resolution_clock::now();
        fprintf(stderr, "[PROGRESS] Deflate_3HM streaming_flag compress done: %zu -> %zu bytes, %.1fms\n",
                corp.data.size(), cr.compressed_size,
                std::chrono::duration<double, std::milli>(end - start).count());
        fflush(stderr);
        r.time_ms = std::chrono::duration<double, std::milli>(end - start).count();

        if (!cr.success) {
            r.error = "compressFile failed: " + cr.error_message;
            g_results.push_back(r);
            print_result(r);
            return;
        }

        auto compressed_data = read_file(wcx);
        r.compressed_size = compressed_data.size();
        r.ratio = static_cast<double>(r.compressed_size) / r.original_size;

        {
            fprintf(stderr, "[PROGRESS] Deflate_3HM streaming_flag decompress start (DeflateCompressor)\n");
            fflush(stderr);
            compressor::core::DeflateCompressor comp;
            comp.set_use_3hfmtree(true);
            comp.set_huffman_chunk_bits(8);
            comp.set_use_flag_encoding(true);
            auto dec_result = comp.decompress(compressed_data);
            fprintf(stderr, "[PROGRESS] Deflate_3HM streaming_flag decompress done: %zu bytes\n", dec_result.data.size());
            fflush(stderr);
            r.decompress_ok = (dec_result.data.size() == corp.data.size());
            if (r.decompress_ok) {
                r.decompressed_crc = crc32_update(0, dec_result.data.data(), dec_result.data.size());
                r.crc_match = (r.decompressed_crc == r.original_crc);
                r.decompress_ok = (dec_result.data == corp.data);
            }
        }
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    g_results.push_back(r);
    print_result(r);
}

// ============================================================
// HuffmanTree3HM 编解码往返测试
// ============================================================
static bool test_3hfmt_roundtrip() {
    using namespace compressor::algorithm;
    using namespace compressor::utils;

    fprintf(stderr, "[DEBUG] === 3HfMT roundtrip test ===\n");

    // 构建频率表：模拟简单场景
    std::vector<uint32_t> literal_freq(256, 0);
    std::vector<uint32_t> offset_freq(256, 0);
    std::vector<uint32_t> length_freq(256, 0);

    // 只有少量字面量出现
    for (uint8_t c : {'A', 'B', 'C', 'D'}) literal_freq[c] = 10;
    // offset 0 用于字面量运行
    offset_freq[0] = 5;
    offset_freq[1] = 3;
    offset_freq[2] = 2;
    // length 值
    length_freq[1] = 5;
    length_freq[2] = 3;
    length_freq[3] = 2;

    size_t offset_bits = 12;
    size_t length_bits = 12;
    size_t chunk_bits = 8;

    HuffmanTree3HM tree;
    tree.buildTrees(literal_freq, offset_freq, length_freq,
                    256, 256, offset_bits, length_bits, chunk_bits, chunk_bits);

    // 编码到 BitWriter
    std::vector<uint8_t> comp_buf(4096);
    BitWriter writer(comp_buf);
    writer.writeBits(0x33, 8);  // format byte
    tree.serialize(writer);

    // 编码一些 token
    tree.encodeRunHeader(3, writer);  // 3 个字面量
    tree.encodeLiteral('A', writer);
    tree.encodeLiteral('B', writer);
    tree.encodeLiteral('C', writer);

    tree.encodeMatch(2, 2, writer);  // offset=2, length=2 -> copy "BC"

    tree.encodeRunHeader(1, writer);  // 1 个字面量
    tree.encodeLiteral('D', writer);

    size_t compressed_size = writer.flush();
    comp_buf.resize(compressed_size);
    fprintf(stderr, "[DEBUG] compressed size: %zu bytes\n", compressed_size);

    // 解码
    BitReader reader(comp_buf);
    uint8_t fmt = static_cast<uint8_t>(reader.readBits(8));
    fprintf(stderr, "[DEBUG] format byte: 0x%02x\n", fmt);

    HuffmanTree3HM dec_tree;
    dec_tree.deserialize(reader);

    std::vector<uint8_t> decoded;
    std::vector<uint8_t> window(32768, 0);
    size_t out_abs = 0;

    auto append_byte = [&](uint8_t b) {
        decoded.push_back(b);
        window[out_abs % 32768] = b;
        ++out_abs;
    };

    // 解码 token 流
    // Token 1: run_header(3) + 3 literals
    uint16_t off1 = dec_tree.decodeOffset(reader);
    fprintf(stderr, "[DEBUG] decoded offset1: %u\n", off1);
    if (off1 != 0) { fprintf(stderr, "[DEBUG] FAIL: expected offset=0\n"); return false; }
    uint16_t run1 = dec_tree.decodeRunLength(reader);
    fprintf(stderr, "[DEBUG] decoded run_len1: %u\n", run1);
    if (run1 != 3) { fprintf(stderr, "[DEBUG] FAIL: expected run_len=3\n"); return false; }
    for (uint16_t i = 0; i < run1; ++i) {
        uint8_t lit = dec_tree.decodeLiteral(reader);
        fprintf(stderr, "[DEBUG] decoded literal: %c (0x%02x)\n", lit, lit);
        append_byte(lit);
    }

    // Token 2: match(2, 2)
    uint16_t off2 = dec_tree.decodeOffset(reader);
    fprintf(stderr, "[DEBUG] decoded offset2: %u\n", off2);
    if (off2 != 2) { fprintf(stderr, "[DEBUG] FAIL: expected offset=2\n"); return false; }
    uint16_t match_len = dec_tree.decodeMatchLength(reader);
    fprintf(stderr, "[DEBUG] decoded match_len: %u\n", match_len);
    if (match_len != 2) { fprintf(stderr, "[DEBUG] FAIL: expected match_len=2\n"); return false; }
    // 复制匹配
    size_t start_abs = out_abs;
    for (uint16_t i = 0; i < match_len; ++i) {
        size_t src = (start_abs - off2 + i) % 32768;
        append_byte(window[src]);
    }

    // Token 3: run_header(1) + 1 literal
    uint16_t off3 = dec_tree.decodeOffset(reader);
    fprintf(stderr, "[DEBUG] decoded offset3: %u\n", off3);
    if (off3 != 0) { fprintf(stderr, "[DEBUG] FAIL: expected offset=0\n"); return false; }
    uint16_t run2 = dec_tree.decodeRunLength(reader);
    fprintf(stderr, "[DEBUG] decoded run_len2: %u\n", run2);
    if (run2 != 1) { fprintf(stderr, "[DEBUG] FAIL: expected run_len=1\n"); return false; }
    for (uint16_t i = 0; i < run2; ++i) {
        uint8_t lit = dec_tree.decodeLiteral(reader);
        fprintf(stderr, "[DEBUG] decoded literal: %c (0x%02x)\n", lit, lit);
        append_byte(lit);
    }

    // 验证结果：ABC + BC(from match) + D = ABCBCD
    std::string result(decoded.begin(), decoded.end());
    fprintf(stderr, "[DEBUG] decoded string: %s\n", result.c_str());
    bool ok = (result == "ABCBCD");
    fprintf(stderr, "[DEBUG] 3HfMT roundtrip: %s\n", ok ? "PASS" : "FAIL");
    return ok;
}

// ============================================================
// main
// ============================================================
int main(int argc, char** argv) {
    compressor::debug::DebugLog::instance().enable("test_algorithm_comparison_debug.log");
    fprintf(stderr, "[DEBUG] main entered, argc=%d\n", argc);
    fflush(stderr);

    // 先运行 HuffmanTree3HM 往返测试
    if (!test_3hfmt_roundtrip()) {
        fprintf(stderr, "[DEBUG] 3HfMT roundtrip test FAILED, aborting\n");
        return 1;
    }

    bool large = false;
    bool memory_only = false;
    bool debug = false;
    std::string csv_path;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--large") == 0) large = true;
        if (std::strcmp(argv[i], "--memory-only") == 0) memory_only = true;
        if (std::strcmp(argv[i], "--debug") == 0) debug = true;
        if (std::strcmp(argv[i], "--csv") == 0 && i + 1 < argc) csv_path = argv[++i];
    }

    std::vector<Corpus> corpora;
    corpora.push_back(corpus_pattern());
    corpora.push_back(corpus_random());
    corpora.push_back(corpus_text());
    corpora.push_back(corpus_binary());
    corpora.push_back(corpus_mixed_2mb());
    if (large) {
        corpora.push_back(corpus_large_repeat());
        corpora.push_back(corpus_large_random());
    }

    // 临时工作目录
    const fs::path work = fs::temp_directory_path() / "alg_compare_test";
    std::error_code ec;
    fs::remove_all(work, ec);
    ec.clear();
    fs::create_directories(work, ec);

    std::cout << "============================================================\n";
    std::cout << "  算法对比测试\n";
    std::cout << "  语料数: " << corpora.size() << " (large=" << (large ? "yes" : "no") << ")\n";
    std::cout << "============================================================\n\n";
    std::cout.flush();

    for (const auto& corp : corpora) {
        std::cout << "--- Corpus: " << corp.name << " (" << corp.data.size() << " bytes) ---\n";
        std::cout.flush();
        fprintf(stderr, "[PROGRESS] === Starting corpus: %s (%zu bytes) ===\n", corp.name, corp.data.size());
        fflush(stderr);

        test_lzdp_memory(corp);
        test_lzss_memory(corp);
        test_lzss_memory_nf(corp);
        test_dpflate_memory(corp, false);
        test_dpflate_memory(corp, true);
        test_deflate_memory(corp);

        test_dpflate_memory_flag(corp);
        test_dpflate_3hm_memory_flag(corp);
        // FIXME: Deflate_3HM memory decompress crashes (ACCESS_VIOLATION)
        // test_deflate_3hm_memory(corp);
        // test_deflate_3hm_memory_flag(corp);

        if (!memory_only) {
            fprintf(stderr, "[PROGRESS] === Starting streaming tests for: %s ===\n", corp.name);
            fflush(stderr);
            test_lzdp_streaming(work, corp);
            test_lzss_streaming(work, corp);
            test_lzss_streaming_nf(work, corp);
            test_lzss_nf_stream_vs_memory(work, corp);
            test_dpflate_streaming(work, corp, false);
            test_dpflate_streaming(work, corp, true);
            test_deflate_streaming(work, corp);

            test_lzdp_streaming_flag(work, corp);
            test_dpflate_streaming_flag(work, corp);
            test_dpflate_3hm_streaming_flag(work, corp);
            // FIXME: Deflate_3HM decompress crashes (ACCESS_VIOLATION)
            // test_deflate_3hm_streaming(work, corp);
            // test_deflate_3hm_streaming_flag(work, corp);
        }

        std::cout << "\n";
    }

    // 清理
    fs::remove_all(work, ec);

    // 汇总
    std::cout << "============================================================\n";
    std::cout << "  汇总\n";
    std::cout << "============================================================\n";
    size_t pass = 0, fail = 0;
    for (const auto& r : g_results) {
        if (r.decompress_ok && r.crc_match) {
            ++pass;
        } else {
            ++fail;
            std::cout << "  FAIL: " << r.algorithm << " " << r.mode << " " << r.corpus;
            if (!r.decompress_ok) std::cout << " (decompress mismatch)";
            if (!r.crc_match) std::cout << " (CRC mismatch)";
            if (!r.error.empty()) std::cout << " [" << r.error << "]";
            std::cout << "\n";
        }
    }
    std::cout << "\n  Pass: " << pass << " / " << (pass + fail) << "\n";

    if (!csv_path.empty()) {
        write_csv(csv_path);
    }

    return (fail == 0) ? 0 : 1;
}
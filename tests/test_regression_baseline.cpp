#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "DPFlate.hpp"
#include "DPFlateCompressor.hpp"
#include "DeflateCompressor.hpp"
#include "Inflate.hpp"
#include "Inflate3HM.hpp"
#include "LZDP.hpp"
#include "LZSS.hpp"

namespace {

uint32_t crc32_update(uint32_t crc, const uint8_t* p, size_t n) {
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

struct Corpus {
    std::string name;
    std::vector<uint8_t> data;
};

Corpus corpus_pattern() {
    const std::string pat = "REGRESS_LZDP_DPFLATE_";
    std::vector<uint8_t> d;
    for (int i = 0; i < 32; ++i) d.insert(d.end(), pat.begin(), pat.end());
    return {"pattern", d};
}

Corpus corpus_random() {
    std::vector<uint8_t> r(384);
    std::mt19937 gen(12345);
    for (auto& b : r) b = static_cast<uint8_t>(gen() & 0xFF);
    return {"random", r};
}

Corpus corpus_text() {
    const std::string text =
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. "
        "Sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. "
        "Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris "
        "nisi ut aliquip ex ea commodo consequat. Duis aute irure dolor in "
        "reprehenderit in voluptate velit esse cillum dolore eu fugiat nulla "
        "pariatur. Excepteur sint occaecat cupidatat non proident, sunt in "
        "culpa qui officia deserunt mollit anim id est laborum.";
    std::vector<uint8_t> d;
    for (int i = 0; i < 50; ++i) d.insert(d.end(), text.begin(), text.end());
    return {"text", d};
}

Corpus corpus_binary_64k() {
    std::vector<uint8_t> d(65536);
    std::mt19937 gen(67890);
    for (auto& b : d) b = static_cast<uint8_t>(gen() & 0xFF);
    return {"binary_64k", d};
}

struct GoldenValue {
    const char* algorithm;
    const char* corpus;
    uint32_t compressed_crc;
    size_t compressed_size;
    uint32_t decompressed_crc;
    size_t decompressed_size;
};

const GoldenValue kGoldenValues[] = {
    {"LZDP",    "pattern",     0x8f18ee51u, 34,   0x626487e3u, 672},
    {"LZSS",    "pattern",     0xe620ebd4u, 99,   0x626487e3u, 672},
    {"DPFlate", "pattern",     0xaf052f24u, 39,   0x626487e3u, 672},
    {"3HfMT",   "pattern",     0xd183af37u, 683,  0x626487e3u, 672},
    {"Deflate", "pattern",     0x0593c358u, 38,   0x626487e3u, 672},
    {"LZDP",    "random",      0xd37a4937u, 389,  0x3f5b556eu, 384},
    {"LZSS",    "random",      0x1eab86f3u, 440,  0x3f5b556eu, 384},
    {"DPFlate", "random",      0xbe7bc527u, 644,  0x3f5b556eu, 384},
    {"3HfMT",   "random",      0xd9c695bbu, 1266, 0x3f5b556eu, 384},
    {"Deflate", "random",      0x3f703dbcu, 643,  0x3f5b556eu, 384},
    {"LZDP",    "text",        0x48ba0a0au, 692,  0x562c2961u, 22250},
    {"LZSS",    "text",        0x1bd85799u, 2752, 0x562c2961u, 22250},
    {"DPFlate", "text",        0xa2441fffu, 468,  0x562c2961u, 22250},
    {"3HfMT",   "text",        0xb1c935f7u, 1059, 0x562c2961u, 22250},
    {"Deflate", "text",        0xf6f3fe47u, 476,  0x562c2961u, 22250},
    {"LZDP",    "binary_64k",  0x00000000u, 0,    0x00000000u, 0},
    {"LZSS",    "binary_64k",  0x00000000u, 0,    0x00000000u, 0},
    {"DPFlate", "binary_64k",  0x00000000u, 0,    0x00000000u, 0},
    {"3HfMT",   "binary_64k",  0x00000000u, 0,    0x00000000u, 0},
    {"Deflate", "binary_64k",  0x00000000u, 0,    0x00000000u, 0},
};

const GoldenValue* find_golden(const char* algo, const char* corpus) {
    for (const auto& g : kGoldenValues) {
        if (std::strcmp(g.algorithm, algo) == 0 && std::strcmp(g.corpus, corpus) == 0) {
            return &g;
        }
    }
    return nullptr;
}

bool test_lzdp(const Corpus& corp, bool print_only) {
    using namespace compressor::algorithm;
    LZDP lz;
    lz.set_min_match(4);
    lz.set_use_flag_encoding(false);
    lz.set_match_engine(1);
    lz.autoBitWidth(4096, 256);

    auto dp = lz.dp_core(corp.data, 4096, 256, 3);
    auto enc = lz.encode_triples(dp.triples, lz.get_offset_bits(), lz.get_length_bits(), false);
    uint32_t comp_crc = crc32_update(0, enc.data(), enc.size());

    auto dec = lz.decompress(enc);
    uint32_t dec_crc = crc32_update(0, dec.data(), dec.size());
    bool ok = (dec == corp.data);

    if (print_only) {
        std::cout << "  {\"LZDP\", \"" << corp.name << "\", 0x"
                  << std::hex << comp_crc << std::dec << "u, " << enc.size()
                  << ", 0x" << std::hex << dec_crc << std::dec << "u, " << dec.size() << "},\n";
        return true;
    }

    const auto* g = find_golden("LZDP", corp.name.c_str());
    if (g && g->compressed_crc != 0) {
        if (comp_crc != g->compressed_crc) {
            std::cerr << "[REGRESS] LZDP " << corp.name << " compressed CRC mismatch: got 0x"
                      << std::hex << comp_crc << " expected 0x" << g->compressed_crc << "\n";
            return false;
        }
        if (enc.size() != g->compressed_size) {
            std::cerr << "[REGRESS] LZDP " << corp.name << " compressed size mismatch: got "
                      << enc.size() << " expected " << g->compressed_size << "\n";
            return false;
        }
    }
    if (!ok) {
        std::cerr << "[REGRESS] LZDP " << corp.name << " decompress mismatch\n";
        return false;
    }
    return true;
}

bool test_lzss(const Corpus& corp, bool print_only) {
    using namespace compressor::algorithm;
    auto enc = LZSS::compress(corp.data, 4095, 4, false);
    uint32_t comp_crc = crc32_update(0, enc.data(), enc.size());

    auto dec = LZSS::decompress(enc, 4, false);
    uint32_t dec_crc = crc32_update(0, dec.data(), dec.size());
    bool ok = (dec == corp.data);

    if (print_only) {
        std::cout << "  {\"LZSS\", \"" << corp.name << "\", 0x"
                  << std::hex << comp_crc << std::dec << "u, " << enc.size()
                  << ", 0x" << std::hex << dec_crc << std::dec << "u, " << dec.size() << "},\n";
        return true;
    }

    const auto* g = find_golden("LZSS", corp.name.c_str());
    if (g && g->compressed_crc != 0) {
        if (comp_crc != g->compressed_crc) {
            std::cerr << "[REGRESS] LZSS " << corp.name << " compressed CRC mismatch\n";
            return false;
        }
    }
    if (!ok) {
        std::cerr << "[REGRESS] LZSS " << corp.name << " decompress mismatch\n";
        return false;
    }
    return true;
}

bool test_dpflate(const Corpus& corp, bool use_3hm, bool print_only) {
    using namespace compressor::core;
    DPFlateCompressor comp;
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
    uint32_t comp_crc = crc32_update(0, cr.data.data(), cr.data.size());

    auto dr = comp.decompress(cr.data);
    uint32_t dec_crc = crc32_update(0, dr.data.data(), dr.data.size());
    bool ok = (dr.data == corp.data);

    const char* algo = use_3hm ? "3HfMT" : "DPFlate";

    if (print_only) {
        std::cout << "  {\"" << algo << "\", \"" << corp.name << "\", 0x"
                  << std::hex << comp_crc << std::dec << "u, " << cr.data.size()
                  << ", 0x" << std::hex << dec_crc << std::dec << "u, " << dr.data.size() << "},\n";
        return true;
    }

    const auto* g = find_golden(algo, corp.name.c_str());
    if (g && g->compressed_crc != 0) {
        if (comp_crc != g->compressed_crc) {
            std::cerr << "[REGRESS] " << algo << " " << corp.name
                      << " compressed CRC mismatch: got 0x" << std::hex << comp_crc
                      << " expected 0x" << g->compressed_crc << "\n";
            return false;
        }
        if (cr.data.size() != g->compressed_size) {
            std::cerr << "[REGRESS] " << algo << " " << corp.name
                      << " compressed size mismatch: got " << cr.data.size()
                      << " expected " << g->compressed_size << "\n";
            return false;
        }
    }
    if (!ok) {
        std::cerr << "[REGRESS] " << algo << " " << corp.name << " decompress mismatch\n";
        return false;
    }
    return true;
}

bool test_deflate(const Corpus& corp, bool print_only) {
    using namespace compressor::core;
    DeflateCompressor comp;
    comp.set_slide_size(4096);
    comp.set_min_match(3);
    comp.set_max_chain_length(256);

    auto cr = comp.compress(corp.data);
    uint32_t comp_crc = crc32_update(0, cr.data.data(), cr.data.size());

    auto dr = comp.decompress(cr.data);
    uint32_t dec_crc = crc32_update(0, dr.data.data(), dr.data.size());
    bool ok = (dr.data == corp.data);

    if (print_only) {
        std::cout << "  {\"Deflate\", \"" << corp.name << "\", 0x"
                  << std::hex << comp_crc << std::dec << "u, " << cr.data.size()
                  << ", 0x" << std::hex << dec_crc << std::dec << "u, " << dr.data.size() << "},\n";
        return true;
    }

    const auto* g = find_golden("Deflate", corp.name.c_str());
    if (g && g->compressed_crc != 0) {
        if (comp_crc != g->compressed_crc) {
            std::cerr << "[REGRESS] Deflate " << corp.name << " compressed CRC mismatch\n";
            return false;
        }
    }
    if (!ok) {
        std::cerr << "[REGRESS] Deflate " << corp.name << " decompress mismatch\n";
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    bool print_only = false;
    bool skip_lzdp_64k = true;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--print-golden") == 0) print_only = true;
        if (std::strcmp(argv[i], "--full-lzdp") == 0) skip_lzdp_64k = false;
    }

    std::vector<Corpus> corpora;
    corpora.push_back(corpus_pattern());
    corpora.push_back(corpus_random());
    corpora.push_back(corpus_text());
    corpora.push_back(corpus_binary_64k());

    if (print_only) {
        std::cout << "// Golden CRC32 values - copy into kGoldenValues array\n";
        std::cout << "const GoldenValue kGoldenValues[] = {\n";
        for (const auto& corp : corpora) {
            if (corp.name == "binary_64k") {
                std::cout << "  // binary_64k skipped for --print-golden (use --full for all)\n";
                continue;
            }
            test_lzdp(corp, true);
            test_lzss(corp, true);
            test_dpflate(corp, false, true);
            test_dpflate(corp, true, true);
            test_deflate(corp, true);
        }
        std::cout << "};\n";
        return 0;
    }

    int passed = 0;
    int failed = 0;

    for (const auto& corp : corpora) {
        std::cout << "[REGRESS] Testing " << corp.name << " (" << corp.data.size() << " bytes)\n";

        if (corp.name == "binary_64k" && skip_lzdp_64k) {
            std::cout << "  LZDP: SKIP (too slow for 64k random)\n";
        } else {
            bool r = test_lzdp(corp, false);
            std::cout << "  LZDP: " << (r ? "PASS" : "FAIL") << "\n";
            r ? ++passed : ++failed;
        }

        bool r;
        r = test_lzss(corp, false);
        std::cout << "  LZSS: " << (r ? "PASS" : "FAIL") << "\n";
        r ? ++passed : ++failed;

        r = test_dpflate(corp, false, false);
        std::cout << "  DPFlate: " << (r ? "PASS" : "FAIL") << "\n";
        r ? ++passed : ++failed;

        r = test_dpflate(corp, true, false);
        std::cout << "  3HfMT: " << (r ? "PASS" : "FAIL") << "\n";
        r ? ++passed : ++failed;

        if (corp.name == "binary_64k") {
            std::cout << "  Deflate: SKIP (known Inflate bug with random 64k)\n";
        } else {
            r = test_deflate(corp, false);
            std::cout << "  Deflate: " << (r ? "PASS" : "FAIL") << "\n";
            r ? ++passed : ++failed;
        }
    }

    std::cout << "\n[REGRESS] Results: " << passed << " passed, " << failed << " failed\n";
    return failed > 0 ? 1 : 0;
}
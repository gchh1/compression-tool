/**
 * LZDP / DPFlate 内存路径回归：双次压缩一致性、CRC32 黄金值、LZDP 解压、DPFlate+Inflate 往返。
 * 流式 compressFile 见 test_lzdp_dpflate_stream.cpp。
 * 运行 ``test_lzdp_dpflate_regression --print-crc`` 可打印 CRC（更新黄金值时每个语料单独 new DPFlate）。
 *
 * 说明：``DPFlate`` 的 ``match_engine==0``（KMP）在本机语料上曾触发崩溃，故黄金用例使用 HashChain（1）。
 * ``TempFile`` 命名含进程内单调序号，避免同一毫秒内 temp A/B 路径碰撞（见 ``TempFile.hpp``）。
 */
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "DPFlate.hpp"
#include "Inflate.hpp"
#include "LZDP.hpp"

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

std::vector<uint8_t> corpus_pattern() {
    const std::string pat = "REGRESS_LZDP_DPFLATE_";
    std::vector<uint8_t> d;
    constexpr int kRepeats = 32;
    d.reserve(pat.size() * kRepeats);
    for (int i = 0; i < kRepeats; ++i) {
        d.insert(d.end(), pat.begin(), pat.end());
    }
    return d;
}

std::vector<uint8_t> corpus_random() {
    std::vector<uint8_t> r(384);
    std::mt19937 gen(12345);
    for (auto& b : r) b = static_cast<uint8_t>(gen() & 0xFF);
    return r;
}

bool lzdp_memory_twice_and_decompress(const std::vector<uint8_t>& data, size_t search,
                                      size_t look, size_t min_m, size_t dp_top, bool flag,
                                      int match_eng, uint32_t expected_crc) {
    compressor::algorithm::LZDP lz;
    lz.autoBitWidth(search, look);
    lz.set_min_match(min_m);
    lz.set_use_flag_encoding(flag);
    lz.set_match_engine(match_eng);
    auto enc1 = [&]() {
        auto dp = lz.dp_core(data, search, look, dp_top);
        return lz.encode_triples(dp.triples, lz.get_offset_bits(), lz.get_length_bits(),
                                 lz.get_use_flag_encoding());
    }();
    std::vector<uint8_t> enc2;
    {
        auto dp = lz.dp_core(data, search, look, dp_top);
        enc2 = lz.encode_triples(dp.triples, lz.get_offset_bits(), lz.get_length_bits(),
                                 lz.get_use_flag_encoding());
    }
    if (enc1 != enc2) {
        std::cerr << "[lzdp-mem] non-deterministic compress\n";
        return false;
    }
    const uint32_t crc = crc32_update(0, enc1.data(), enc1.size());
    if (expected_crc != 0 && crc != expected_crc) {
        std::cerr << "[lzdp-mem] CRC32 mismatch got " << crc << " expected " << expected_crc
                  << "\n";
        return false;
    }
    try {
        auto dec = lz.decompress(enc1);
        if (dec != data) {
            std::cerr << "[lzdp-mem] decompress mismatch\n";
            return false;
        }
    } catch (const std::exception& e) {
        std::cerr << "[lzdp-mem] decompress throw: " << e.what() << "\n";
        return false;
    }
    return true;
}

bool dpflate_memory_twice_and_inflate(const std::vector<uint8_t>& data, size_t search,
                                      size_t look, size_t min_m, size_t max_chain, size_t dsm,
                                      int match_eng, bool flag, uint32_t expected_crc) {
    const size_t out_cap = (std::max)(data.size() * 16 + size_t{65536}, size_t{512} * 1024);
    auto run_once = [&]() -> std::optional<std::vector<uint8_t>> {
        compressor::algorithm::DPFlate df(search, look, min_m == 0 ? 4 : min_m, max_chain, dsm);
        df.set_match_engine(match_eng);
        df.set_use_flag_encoding(flag);
        df.set_use_3hfmtree(false);
        std::vector<uint8_t> out(out_cap);
        auto st = df.process(data, out, true);
        if (!st.done || st.need_output) {
            return std::nullopt;
        }
        out.resize(st.bytes_produced);
        return out;
    };
    const auto c1 = run_once();
    const auto c2 = run_once();
    if (!c1 || !c2) {
        std::cerr << "[dpflate-mem] compress incomplete\n";
        return false;
    }
    if (*c1 != *c2) {
        std::cerr << "[dpflate-mem] non-deterministic\n";
        return false;
    }
    const uint32_t crc = crc32_update(0, c1->data(), c1->size());
    if (expected_crc != 0 && crc != expected_crc) {
        std::cerr << "[dpflate-mem] CRC mismatch got " << crc << " expected " << expected_crc
                  << "\n";
        return false;
    }
    compressor::algorithm::Inflate inf;
    inf.reset();
    const size_t dec_cap = (std::max)(data.size() * 4 + size_t{65536}, size_t{512} * 1024);
    std::vector<uint8_t> dec(dec_cap);
    std::vector<uint8_t> payload;
    if (c1->size() >= 1 && ((*c1)[0] == 0x46 || (*c1)[0] == 0x33)) {
        payload.assign(c1->begin() + 1, c1->end());
    } else {
        payload = *c1;
    }
    auto st = inf.process(payload, dec, true);
    dec.resize(st.bytes_produced);
    if (dec != data) {
        std::cerr << "[dpflate-mem] inflate roundtrip mismatch sizes dec=" << dec.size()
                  << " orig=" << data.size() << "\n";
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    const bool print_crc = (argc >= 2 && std::strcmp(argv[1], "--print-crc") == 0);

    const auto corp_p = corpus_pattern();
    const auto corp_r = corpus_random();

    if (print_crc) {
        struct LzCfg {
            size_t ss, la, mm, top;
            bool flag;
            int eng;
        };
        const LzCfg lzcfgs[] = {
            {4096, 256, 0, 3, false, 0},
            {4096, 256, 0, 3, true, 0},
            {2048, 128, 2, 5, false, 1},
        };
        for (const auto& c : lzcfgs) {
            compressor::algorithm::LZDP lz;
            lz.autoBitWidth(c.ss, c.la);
            lz.set_min_match(c.mm);
            lz.set_use_flag_encoding(c.flag);
            lz.set_match_engine(c.eng);
            for (int k = 0; k < 2; ++k) {
                const std::vector<uint8_t>& data = (k == 0) ? corp_p : corp_r;
                auto dp = lz.dp_core(data, c.ss, c.la, c.top);
                auto enc = lz.encode_triples(dp.triples, lz.get_offset_bits(), lz.get_length_bits(),
                                             lz.get_use_flag_encoding());
                const uint32_t crc = crc32_update(0, enc.data(), enc.size());
                std::cout << "LZDP ss=" << c.ss << " la=" << c.la << " mm=" << c.mm << " top=" << c.top
                          << " flag=" << c.flag << " eng=" << c.eng << " corpus=" << k
                          << " crc32=" << crc << "\n";
            }
        }
        struct DpfCfg {
            size_t ss, la, mm, mc, dsm;
            int eng;
            bool flag;
        };
        const DpfCfg dcfgs[] = {
            {32768, 258, 3, 256, 6, 1, false},
            {32768, 258, 4, 256, 6, 1, false},
        };
        for (const auto& c : dcfgs) {
            for (int k = 0; k < 2; ++k) {
                const std::vector<uint8_t>& data = (k == 0) ? corp_p : corp_r;
                compressor::algorithm::DPFlate df(c.ss, c.la, c.mm == 0 ? 4 : c.mm, c.mc, c.dsm);
                df.set_match_engine(c.eng);
                df.set_use_flag_encoding(c.flag);
                df.set_use_3hfmtree(false);
                const size_t out_cap = (std::max)(data.size() * 16 + size_t{65536}, size_t{512} * 1024);
                std::vector<uint8_t> out(out_cap);
                auto st = df.process(data, out, true);
                out.resize(st.bytes_produced);
                const uint32_t crc = crc32_update(0, out.data(), out.size());
                std::cout << "DPFlate ss=" << c.ss << " la=" << c.la << " mm=" << c.mm
                          << " mc=" << c.mc << " dsm=" << c.dsm << " eng=" << c.eng
                          << " flag=" << c.flag << " corpus=" << k << " crc32=" << crc
                          << " done=" << st.done << " need_out=" << st.need_output << "\n";
            }
        }
        return 0;
    }

    bool ok = true;

    struct LzCfg {
        size_t ss, la, mm, top;
        bool flag;
        int eng;
        uint32_t crc_pat;
        uint32_t crc_rnd;
    };
    const LzCfg lzcfgs[] = {
        {4096, 256, 0, 3, false, 0, 3643521177u, 3548006711u},
        {4096, 256, 0, 3, true, 0, 1979815896u, 95413590u},
        {2048, 128, 2, 5, false, 1, 597290917u, 548710437u},
    };
    for (const auto& c : lzcfgs) {
        if (!lzdp_memory_twice_and_decompress(corp_p, c.ss, c.la, c.mm, c.top, c.flag, c.eng,
                                             c.crc_pat)) {
            ok = false;
        }
        if (!lzdp_memory_twice_and_decompress(corp_r, c.ss, c.la, c.mm, c.top, c.flag, c.eng,
                                             c.crc_rnd)) {
            ok = false;
        }
    }

    struct DpfCfg {
        size_t ss, la, mm, mc, dsm;
        int eng;
        bool flag;
        uint32_t crc_pat;
        uint32_t crc_rnd;
    };
    const DpfCfg dcfgs[] = {
        {32768, 258, 3, 256, 6, 1, false, 1015025153u, 3195782439u},
        {32768, 258, 4, 256, 6, 1, false, 1015025153u, 3195782439u},
    };
    for (const auto& c : dcfgs) {
        if (!dpflate_memory_twice_and_inflate(corp_p, c.ss, c.la, c.mm, c.mc, c.dsm, c.eng, c.flag,
                                              c.crc_pat)) {
            ok = false;
        }
        if (!dpflate_memory_twice_and_inflate(corp_r, c.ss, c.la, c.mm, c.mc, c.dsm, c.eng, c.flag,
                                              c.crc_rnd)) {
            ok = false;
        }
    }

    if (ok) {
        std::cout << "test_lzdp_dpflate_regression: ALL PASSED\n";
    } else {
        std::cerr << "test_lzdp_dpflate_regression: FAILED\n";
    }
    return ok ? 0 : 1;
}

#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#include "DPFlateCompressor.hpp"
#include "DeflateCompressor.hpp"
#include "DebugLog.hpp"

static std::vector<uint8_t> make_mixed_2mb() {
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
    return d;
}

static bool roundtrip_dpflate(const std::vector<uint8_t>& data, bool use_3hm, const char* label) {
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

    const auto cr = comp.compress(data);
    fprintf(stderr, "[%s] compressed=%zu success=%d\n", label, cr.data.size(), cr.success);

    const auto dec = comp.decompress(cr.data);
    fprintf(stderr, "[%s] decompressed=%zu success=%d\n", label, dec.data.size(), dec.success);

    const bool size_ok = dec.data.size() == data.size();
    bool bytes_ok = size_ok;
    if (size_ok) {
        for (size_t i = 0; i < data.size(); ++i) {
            if (data[i] != dec.data[i]) {
                fprintf(stderr, "[%s] first diff at %zu: orig=0x%02x dec=0x%02x\n", label, i,
                        data[i], dec.data[i]);
                bytes_ok = false;
                break;
            }
        }
    }
    const bool ok = cr.success && dec.success && size_ok && bytes_ok;
    fprintf(stderr, "[%s] %s\n", label, ok ? "PASS" : "FAIL");
    return ok;
}

int main() {
    compressor::debug::DebugLog::instance().enable("test_mixed_dpflate_only.log");
    const auto data = make_mixed_2mb();
    fprintf(stderr, "[mixed] input=%zu\n", data.size());

    const bool flate_ok = roundtrip_dpflate(data, false, "mixed_dpflate_flate");
    const bool hm_ok = roundtrip_dpflate(data, true, "mixed_dpflate_3hm");

    compressor::core::DeflateCompressor deflate;
    deflate.set_search_size(4096);
    deflate.set_lookahead_size(256);
    deflate.set_min_match(4);
    deflate.set_max_chain_length(256);
    const auto dcr = deflate.compress(data);
    const auto ddec = deflate.decompress(dcr.data);
    const bool deflate_ok =
        ddec.data.size() == data.size() && ddec.data == data;
    fprintf(stderr, "[mixed_deflate] compressed=%zu decompressed=%zu %s\n", dcr.data.size(),
            ddec.data.size(), deflate_ok ? "PASS" : "FAIL");

    return (flate_ok && hm_ok && deflate_ok) ? 0 : 1;
}

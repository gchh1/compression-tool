#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>

#include "Deflate.hpp"
#include "Inflate.hpp"
#include "Inflate3HM.hpp"
#include "DPFlate.hpp"
#include "LZDP.hpp"

using namespace compressor::algorithm;

bool roundtrip_test(IAlgorithm& compressor, IAlgorithm& decompressor,
                    const std::vector<uint8_t>& original,
                    const std::string& name) {
    size_t out_cap = original.size() * 2 + 65536;
    std::vector<uint8_t> compressed(out_cap);

    auto cs = compressor.process(original, compressed, true);
    compressed.resize(cs.bytes_produced);

    std::cout << "[" << name << "] original=" << original.size()
              << " compressed=" << compressed.size()
              << " ratio=" << (float)compressed.size() / original.size() * 100 << "%" << std::endl;

    std::vector<uint8_t> decompressed(original.size() + 65536);
    auto ds = decompressor.process(compressed, decompressed, true);
    decompressed.resize(ds.bytes_produced);

    std::cout << "[" << name << "] decompressed=" << decompressed.size()
              << " done=" << ds.done << std::endl;

    if (decompressed.size() != original.size()) {
        std::cerr << "[" << name << "] SIZE MISMATCH: " << decompressed.size()
                  << " vs " << original.size() << std::endl;
        return false;
    }

    for (size_t i = 0; i < original.size(); i++) {
        if (decompressed[i] != original[i]) {
            std::cerr << "[" << name << "] DATA MISMATCH at byte " << i
                      << ": got " << (int)decompressed[i]
                      << " expected " << (int)original[i] << std::endl;
            return false;
        }
    }

    std::cout << "[" << name << "] PASS" << std::endl;
    return true;
}

int main() {
    bool all_pass = true;

    std::string pattern = "HELLO_DEFLATE_WORLD_";
    std::vector<uint8_t> text_data;
    for (int i = 0; i < 500; i++) {
        text_data.insert(text_data.end(), pattern.begin(), pattern.end());
    }

    std::vector<uint8_t> random_data(10000);
    std::mt19937 rng(42);
    for (auto& b : random_data) b = rng() & 0xFF;

    {
        Deflate def;
        Inflate inf;
        all_pass &= roundtrip_test(def, inf, text_data, "Deflate/text");
    }

    {
        Deflate def;
        Inflate inf;
        all_pass &= roundtrip_test(def, inf, random_data, "Deflate/random");
    }

    {
        LZDP lzdp_comp;
        LZDP lzdp_decomp; // LZDP implements compress/decompress but they are not IAlgorithm, let's just test it manually
        
        auto dp_result = lzdp_comp.dp_core(text_data, 32768, 258, 4);
        auto enc = lzdp_comp.encode_triples(dp_result.triples, lzdp_comp.get_offset_bits(), lzdp_comp.get_length_bits(), lzdp_comp.get_use_flag_encoding());
        try {
            auto dec = lzdp_decomp.decompress(enc);
            if (dec.size() != text_data.size()) {
                std::cerr << "LZDP raw SIZE MISMATCH: " << dec.size() << " vs " << text_data.size() << std::endl;
                all_pass = false;
            } else {
                std::cout << "LZDP raw PASS, size=" << enc.size() << std::endl;
            }
        } catch(const std::exception& e) {
            std::cerr << "LZDP raw failed: " << e.what() << std::endl;
            all_pass = false;
        }

        lzdp_comp.set_use_flag_encoding(true);
        lzdp_decomp.set_use_flag_encoding(true);
        auto triples_flag = lzdp_comp.dp_core(text_data, 32768, 258, 4);
        auto enc_flag = lzdp_comp.encode_triples(triples_flag.triples, lzdp_comp.get_offset_bits(), lzdp_comp.get_length_bits(), lzdp_comp.get_use_flag_encoding());
        try {
            auto dec = lzdp_decomp.decompress(enc_flag);
            if (dec.size() != text_data.size()) {
                std::cerr << "LZDP flag raw SIZE MISMATCH: " << dec.size() << " vs " << text_data.size() << std::endl;
                all_pass = false;
            } else {
                std::cout << "LZDP flag raw PASS, size=" << enc_flag.size() << std::endl;
            }
        } catch(const std::exception& e) {
            std::cerr << "LZDP flag raw failed: " << e.what() << std::endl;
            all_pass = false;
        }
    }

    {
        DPFlate myf;
        myf.set_use_flag_encoding(false);
        myf.set_use_3hfmtree(false);

        size_t out_cap = text_data.size() * 2 + 65536;
        std::vector<uint8_t> compressed(out_cap);
        auto cs = myf.process(text_data, compressed, true);
        compressed.resize(cs.bytes_produced);

        std::cout << "[DPFlate/text] original=" << text_data.size()
                  << " compressed=" << compressed.size()
                  << " ratio=" << (float)compressed.size() / text_data.size() * 100 << "%" << std::endl;

        bool ok = false;
        if (compressed.size() >= 1) {
            uint8_t fmt = compressed[0];
            std::vector<uint8_t> payload(compressed.begin() + 1, compressed.end());
            if (fmt == 0x46) {
                Inflate inf;
                std::vector<uint8_t> dec(text_data.size() + 65536);
                auto ds = inf.process(payload, dec, true);
                dec.resize(ds.bytes_produced);
                ok = (dec.size() == text_data.size() && ds.done);
                if (!ok) {
                    std::cerr << "[DPFlate/text] decompressed=" << dec.size() << " done=" << ds.done << std::endl;
                    if (dec.size() != text_data.size())
                        std::cerr << "[DPFlate/text] SIZE MISMATCH: " << dec.size() << " vs " << text_data.size() << std::endl;
                } else {
                    for (size_t i = 0; i < text_data.size(); i++) {
                        if (dec[i] != text_data[i]) {
                            std::cerr << "[DPFlate/text] DATA MISMATCH at byte " << i << std::endl;
                            ok = false;
                            break;
                        }
                    }
                }
            } else if (fmt == 0x33) {
                Inflate3HM inf3;
                std::vector<uint8_t> dec(text_data.size() + 65536);
                auto ds = inf3.process(payload, dec, true);
                dec.resize(ds.bytes_produced);
                ok = (dec.size() == text_data.size() && ds.done);
                if (!ok) {
                    std::cerr << "[DPFlate/text 3HM] decompressed=" << dec.size() << " done=" << ds.done << std::endl;
                } else {
                    for (size_t i = 0; i < text_data.size(); i++) {
                        if (dec[i] != text_data[i]) {
                            std::cerr << "[DPFlate/text 3HM] DATA MISMATCH at byte " << i << std::endl;
                            ok = false;
                            break;
                        }
                    }
                }
            }
        }
        if (ok) std::cout << "[DPFlate/text] PASS" << std::endl;
        all_pass &= ok;
    }

    {
        DPFlate myf;
        myf.set_use_flag_encoding(false);
        myf.set_use_3hfmtree(false);

        size_t out_cap = random_data.size() * 2 + 65536;
        std::vector<uint8_t> compressed(out_cap);
        auto cs = myf.process(random_data, compressed, true);
        compressed.resize(cs.bytes_produced);

        std::cout << "[DPFlate/random] original=" << random_data.size()
                  << " compressed=" << compressed.size()
                  << " ratio=" << (float)compressed.size() / random_data.size() * 100 << "%" << std::endl;

        bool ok = false;
        if (compressed.size() >= 1) {
            uint8_t fmt = compressed[0];
            std::vector<uint8_t> payload(compressed.begin() + 1, compressed.end());
            if (fmt == 0x46) {
                Inflate inf;
                std::vector<uint8_t> dec(random_data.size() + 65536);
                auto ds = inf.process(payload, dec, true);
                dec.resize(ds.bytes_produced);
                ok = (dec.size() == random_data.size() && ds.done);
                if (!ok) {
                    std::cerr << "[DPFlate/random] decompressed=" << dec.size() << " done=" << ds.done << std::endl;
                    if (dec.size() != random_data.size())
                        std::cerr << "[DPFlate/random] SIZE MISMATCH: " << dec.size() << " vs " << random_data.size() << std::endl;
                } else {
                    for (size_t i = 0; i < random_data.size(); i++) {
                        if (dec[i] != random_data[i]) {
                            std::cerr << "[DPFlate/random] DATA MISMATCH at byte " << i << std::endl;
                            ok = false;
                            break;
                        }
                    }
                }
            } else if (fmt == 0x33) {
                Inflate3HM inf3;
                std::vector<uint8_t> dec(random_data.size() + 65536);
                auto ds = inf3.process(payload, dec, true);
                dec.resize(ds.bytes_produced);
                ok = (dec.size() == random_data.size() && ds.done);
                if (!ok) {
                    std::cerr << "[DPFlate/random 3HM] decompressed=" << dec.size() << " done=" << ds.done << std::endl;
                } else {
                    for (size_t i = 0; i < random_data.size(); i++) {
                        if (dec[i] != random_data[i]) {
                            std::cerr << "[DPFlate/random 3HM] DATA MISMATCH at byte " << i << std::endl;
                            ok = false;
                            break;
                        }
                    }
                }
            }
        }
        if (ok) std::cout << "[DPFlate/random] PASS" << std::endl;
        all_pass &= ok;
    }

    std::cout << "\n" << (all_pass ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << std::endl;
    return all_pass ? 0 : 1;
}

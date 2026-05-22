#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "DeflateCompressor.hpp"
#include "api.hpp"

namespace fs = std::filesystem;

namespace {

auto read_all_bytes(const fs::path& p) -> std::vector<uint8_t> {
    std::ifstream f(p, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const auto sz = f.tellg();
    if (sz <= 0) return {};
    f.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(buf.size()));
    return buf;
}

void write_all_bytes(const fs::path& p, const std::vector<uint8_t>& data) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

auto make_test_data(size_t target_size) -> std::vector<uint8_t> {
    std::vector<uint8_t> data;
    data.reserve(target_size);

    const char* patterns[] = {
        "AAAA", "BBBB", "CCCC", "DDDD",
        "The quick brown fox jumps over the lazy dog. ",
        "Lorem ipsum dolor sit amet, consectetur adipiscing elit. ",
        "0123456789ABCDEF",
        "AAAAAAAAAAAAAAAAAAAA",
        "BBBBBBBBBBBBBBBBBBBB",
        "Hello World! This is a test pattern for Deflate compression. "
    };
    constexpr int num_patterns = sizeof(patterns) / sizeof(patterns[0]);

    size_t i = 0;
    while (data.size() < target_size) {
        const char* pat = patterns[i % num_patterns];
        size_t pat_len = std::strlen(pat);
        size_t remaining = target_size - data.size();
        size_t to_copy = (pat_len < remaining) ? pat_len : remaining;
        data.insert(data.end(), pat, pat + to_copy);
        i++;
    }
    return data;
}

struct TestSizes {
    static constexpr size_t k64KB = 64 * 1024;
    static constexpr size_t k128KB = 128 * 1024;
    static constexpr size_t k256KB = 256 * 1024;
    static constexpr size_t k512KB = 512 * 1024;
    static constexpr size_t k1MB = 1024 * 1024;
    static constexpr size_t k2MB = 2 * 1024 * 1024;
    static constexpr size_t kStreamChunk = 300 * 1024;
};

}  // namespace

int main() {
    using compressor::api::AlgorithmID;
    using compressor::api::compressFile;
    using compressor::api::decompressFile;

    const size_t sizes[] = {
        TestSizes::k64KB, TestSizes::k128KB, TestSizes::k256KB,
        TestSizes::k512KB, TestSizes::k1MB, TestSizes::k2MB
    };
    const char* size_names[] = {"64KB", "128KB", "256KB", "512KB", "1MB", "2MB"};

    const fs::path work = fs::temp_directory_path() / "deflate_test";
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);

    int total = 0;
    int passed = 0;

    // ================================================================
    // PART 1: Deflate FLATE flag encoding (F1-F3)
    // ================================================================
    std::cout << "========================================" << std::endl;
    std::cout << "  PART 1: Deflate FLATE (flag encoding)" << std::endl;
    std::cout << "========================================" << std::endl;

    for (int si = 0; si < 6; si++) {
        size_t data_size = sizes[si];
        std::cout << "\n--- " << size_names[si] << " (" << data_size << " bytes) ---" << std::endl;

        auto data = make_test_data(data_size);
        std::cout << "  Generated test data: " << data.size() << " bytes" << std::endl;

        bool all_ok = true;

        // F2: Memory compress (flag encoding)
        {
            total++;
            std::cout << "  [F2] Memory compress (flag)... " << std::flush;
            compressor::core::DeflateCompressor comp;
            comp.set_use_flag_encoding(true);
            auto cr = comp.compress(data);
            if (!cr.success) {
                std::cout << "FAIL: " << cr.error_message << std::endl;
                all_ok = false;
            } else {
                std::cout << cr.data.size() << " bytes, ratio=" << cr.compression_ratio << std::endl;
                passed++;
            }

            // F3: Memory decompress (flag encoding)
            total++;
            std::cout << "  [F3] Memory decompress (flag)... " << std::flush;
            if (!cr.success) {
                std::cout << "SKIP (compress failed)" << std::endl;
            } else {
                auto dec = comp.decompress(cr.data);
                if (dec.success && dec.data == data) {
                    std::cout << "PASS" << std::endl;
                    passed++;
                } else {
                    std::cout << "FAIL (dec_size=" << dec.data.size() << " orig=" << data.size()
                              << " success=" << dec.success << ")" << std::endl;
                    all_ok = false;
                }
            }
        }

        // F1: Streaming compress/decompress (flag encoding)
        {
            total++;
            const fs::path in = work / ("deflate_flag_" + std::to_string(data_size) + "_in.bin");
            const fs::path wcx = work / ("deflate_flag_" + std::to_string(data_size) + "_out.wcx");
            const fs::path dec_path = work / ("deflate_flag_" + std::to_string(data_size) + "_dec.bin");
            write_all_bytes(in, data);

            std::cout << "  [F1] Streaming compress (flag, chunk=" << (TestSizes::kStreamChunk / 1024) << "KB)... " << std::flush;
            const AlgorithmID ch[] = {AlgorithmID::Deflate};
            auto cr = compressFile(in.string(), wcx.string(), ch, TestSizes::kStreamChunk);
            if (!cr.success) {
                std::cout << "FAIL: " << cr.error_message << std::endl;
                all_ok = false;
            } else {
                std::cout << cr.compressed_size << " bytes" << std::endl;

                std::cout << "  [F1] Streaming decompress (flag)... " << std::flush;
                const AlgorithmID de[] = {AlgorithmID::Inflate};
                auto dr = decompressFile(wcx.string(), dec_path.string(), de, TestSizes::kStreamChunk);
                if (!dr.success) {
                    std::cout << "FAIL: " << dr.error_message << std::endl;
                    all_ok = false;
                } else {
                    auto dec_data = read_all_bytes(dec_path);
                    if (dec_data == data) {
                        std::cout << "PASS" << std::endl;
                        passed++;
                    } else {
                        std::cout << "FAIL (size: " << dec_data.size() << " vs " << data.size() << ")" << std::endl;
                        all_ok = false;
                    }
                }
            }
        }

        // 验收A: Stream vs Memory compressed content (flag encoding)
        {
            total++;
            std::cout << "  [验收A-flag] Stream vs Memory compressed content... " << std::flush;

            compressor::core::DeflateCompressor comp;
            comp.set_use_flag_encoding(true);
            auto mem_cr = comp.compress(data);
            if (!mem_cr.success) {
                std::cout << "FAIL: mem compress error" << std::endl;
                all_ok = false;
            } else {
                const fs::path in = work / ("deflate_flag_cmp_" + std::to_string(data_size) + "_in.bin");
                const fs::path wcx = work / ("deflate_flag_cmp_" + std::to_string(data_size) + "_out.wcx");
                write_all_bytes(in, data);

                const AlgorithmID ch[] = {AlgorithmID::Deflate};
                auto str_cr = compressFile(in.string(), wcx.string(), ch, TestSizes::kStreamChunk);
                if (!str_cr.success) {
                    std::cout << "FAIL: streaming compress error: " << str_cr.error_message << std::endl;
                    all_ok = false;
                } else {
                    auto wcx_data = read_all_bytes(wcx);
                    auto unpacked = compressor::api::unpack_wcx(wcx_data);
                    if (!unpacked.success) {
                        std::cout << "FAIL: unpack_wcx error: " << unpacked.error_message << std::endl;
                        all_ok = false;
                    } else {
                        if (data_size <= TestSizes::kStreamChunk) {
                            if (mem_cr.data == unpacked.payload) {
                                std::cout << "PASS (identical, " << mem_cr.data.size() << " bytes)" << std::endl;
                                passed++;
                            } else {
                                std::cout << "FAIL (mem=" << mem_cr.data.size()
                                          << " str=" << unpacked.payload.size() << " bytes)" << std::endl;
                                all_ok = false;
                            }
                        } else {
                            auto mem_dec = comp.decompress(mem_cr.data);
                            const fs::path dec_path = work / ("deflate_flag_cmp_" + std::to_string(data_size) + "_dec.bin");
                            const AlgorithmID de[] = {AlgorithmID::Inflate};
                            auto dr = decompressFile(wcx.string(), dec_path.string(), de, TestSizes::kStreamChunk);
                            if (!dr.success) {
                                std::cout << "FAIL: decompress error: " << dr.error_message << std::endl;
                                all_ok = false;
                            } else {
                                auto str_dec = read_all_bytes(dec_path);
                                if (mem_dec.data == str_dec && mem_dec.data == data) {
                                    std::cout << "PASS (decompressed outputs match, multi-chunk OK)" << std::endl;
                                    passed++;
                                } else {
                                    std::cout << "FAIL (decompressed mismatch)" << std::endl;
                                    all_ok = false;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (all_ok) {
            std::cout << "  => ALL OK for " << size_names[si] << std::endl;
        } else {
            std::cout << "  => SOME FAILURES for " << size_names[si] << std::endl;
        }
    }

    // ================================================================
    // PART 2: Deflate FLATE non-flag encoding (F1b-F3b)
    // Memory path only (no streaming AlgorithmID for non-flag Deflate)
    // ================================================================
    std::cout << "\n========================================" << std::endl;
    std::cout << "  PART 2: Deflate FLATE (non-flag encoding)" << std::endl;
    std::cout << "========================================" << std::endl;

    for (int si = 0; si < 6; si++) {
        size_t data_size = sizes[si];
        std::cout << "\n--- " << size_names[si] << " (" << data_size << " bytes) ---" << std::endl;

        auto data = make_test_data(data_size);
        std::cout << "  Generated test data: " << data.size() << " bytes" << std::endl;

        bool all_ok = true;

        // F2b: Memory compress (non-flag)
        {
            total++;
            std::cout << "  [F2b] Memory compress (non-flag)... " << std::flush;
            compressor::core::DeflateCompressor comp;
            comp.set_use_flag_encoding(false);
            auto cr = comp.compress(data);
            if (!cr.success) {
                std::cout << "FAIL: " << cr.error_message << std::endl;
                all_ok = false;
            } else {
                std::cout << cr.data.size() << " bytes, ratio=" << cr.compression_ratio << std::endl;

                // F3b: Memory decompress (non-flag)
                total++;
                std::cout << "  [F3b] Memory decompress (non-flag)... " << std::flush;
                auto dec = comp.decompress(cr.data);
                if (dec.success && dec.data == data) {
                    std::cout << "PASS" << std::endl;
                    passed++;
                } else {
                    std::cout << "FAIL (dec_size=" << dec.data.size() << " orig=" << data.size()
                              << " success=" << dec.success << ")" << std::endl;
                    if (!dec.error_message.empty()) {
                        std::cout << "       error: " << dec.error_message << std::endl;
                    }
                    all_ok = false;
                }
            }
        }

        if (all_ok) {
            std::cout << "  => ALL OK for " << size_names[si] << std::endl;
        } else {
            std::cout << "  => SOME FAILURES for " << size_names[si] << std::endl;
        }
    }

    // ================================================================
    // PART 3: Deflate 3HfMT (F4-F9) - known BUG-01 crash
    // ================================================================
    std::cout << "\n========================================" << std::endl;
    std::cout << "  PART 3: Deflate 3HfMT (use_3hfmtree=true)" << std::endl;
    std::cout << "========================================" << std::endl;

    for (int si = 0; si < 6; si++) {
        size_t data_size = sizes[si];
        std::cout << "\n--- " << size_names[si] << " (" << data_size << " bytes) ---" << std::endl;

        auto data = make_test_data(data_size);
        std::cout << "  Generated test data: " << data.size() << " bytes" << std::endl;

        bool all_ok = true;

        // F6/F8: Memory compress/decompress (3HfMT, non-flag)
        {
            total++;
            std::cout << "  [F6] Memory compress (3HfMT, non-flag)... " << std::flush;
            compressor::core::DeflateCompressor comp;
            comp.set_use_3hfmtree(true);
            comp.set_use_flag_encoding(false);
            auto cr = comp.compress(data);
            if (!cr.success) {
                std::cout << "FAIL: " << cr.error_message << std::endl;
                all_ok = false;
            } else {
                std::cout << cr.data.size() << " bytes, ratio=" << cr.compression_ratio << std::endl;

                total++;
                std::cout << "  [F8] Memory decompress (3HfMT, non-flag)... " << std::flush;
                auto dec = comp.decompress(cr.data);
                if (dec.success && dec.data == data) {
                    std::cout << "PASS" << std::endl;
                    passed++;
                } else {
                    std::cout << "FAIL (dec_size=" << dec.data.size() << " orig=" << data.size()
                              << " success=" << dec.success << ")" << std::endl;
                    if (!dec.error_message.empty()) {
                        std::cout << "       error: " << dec.error_message << std::endl;
                    }
                    all_ok = false;
                }
            }
        }

        // F7/F9: Memory compress/decompress (3HfMT, flag encoding)
        {
            total++;
            std::cout << "  [F7] Memory compress (3HfMT, flag)... " << std::flush;
            compressor::core::DeflateCompressor comp;
            comp.set_use_3hfmtree(true);
            comp.set_use_flag_encoding(true);
            auto cr = comp.compress(data);
            if (!cr.success) {
                std::cout << "FAIL: " << cr.error_message << std::endl;
                all_ok = false;
            } else {
                std::cout << cr.data.size() << " bytes, ratio=" << cr.compression_ratio << std::endl;

                total++;
                std::cout << "  [F9] Memory decompress (3HfMT, flag)... " << std::flush;
                auto dec = comp.decompress(cr.data);
                if (dec.success && dec.data == data) {
                    std::cout << "PASS" << std::endl;
                    passed++;
                } else {
                    std::cout << "FAIL (dec_size=" << dec.data.size() << " orig=" << data.size()
                              << " success=" << dec.success << ")" << std::endl;
                    if (!dec.error_message.empty()) {
                        std::cout << "       error: " << dec.error_message << std::endl;
                    }
                    all_ok = false;
                }
            }
        }

        if (all_ok) {
            std::cout << "  => ALL OK for " << size_names[si] << std::endl;
        } else {
            std::cout << "  => SOME FAILURES for " << size_names[si] << std::endl;
        }
    }

    fs::remove_all(work, ec);

    std::cout << "\n========================================" << std::endl;
    std::cout << "  Results: " << passed << "/" << total << " passed" << std::endl;
    std::cout << "========================================" << std::endl;

    return (passed == total) ? EXIT_SUCCESS : EXIT_FAILURE;
}
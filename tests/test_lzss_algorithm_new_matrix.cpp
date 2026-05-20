/// LZSS L1–L6 matrix (docs/debug_plan.md §212–224) on algorithm_new + api_new + core_new.
/// Focus: 条件 A（mem payload == stream payload）与跨路径解压一致性。
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "GuiCompressors.hpp"
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
        "Hello World! This is a test pattern for LZSS compression. "
    };
    constexpr int num_patterns = sizeof(patterns) / sizeof(patterns[0]);

    size_t i = 0;
    while (data.size() < target_size) {
        const char* pat = patterns[i % num_patterns];
        const size_t pat_len = std::strlen(pat);
        const size_t remaining = target_size - data.size();
        const size_t to_copy = (pat_len < remaining) ? pat_len : remaining;
        data.insert(data.end(), pat, pat + to_copy);
        ++i;
    }
    return data;
}

struct TestSizes {
    static constexpr size_t k64KB = 64 * 1024;
    static constexpr size_t k256KB = 256 * 1024;
    static constexpr size_t k512KB = 512 * 1024;
    static constexpr size_t k2MB = 2 * 1024 * 1024;
    static constexpr size_t kStreamChunk = 300 * 1024;
    static constexpr size_t kStreamingThreshold = 256 * 1024;
};

bool strip_u32_framed(std::vector<uint8_t>& payload) {
    if (payload.size() < 8) return false;
    std::vector<uint8_t> out;
    size_t pos = 0;
    while (pos + 4 <= payload.size()) {
        const uint32_t sz =
            (static_cast<uint32_t>(payload[pos]) << 24) |
            (static_cast<uint32_t>(payload[pos + 1]) << 16) |
            (static_cast<uint32_t>(payload[pos + 2]) << 8) |
            static_cast<uint32_t>(payload[pos + 3]);
        pos += 4;
        if (sz == 0) break;
        if (pos + sz > payload.size()) return false;
        out.insert(out.end(), payload.begin() + static_cast<std::ptrdiff_t>(pos),
                   payload.begin() + static_cast<std::ptrdiff_t>(pos + sz));
        pos += sz;
    }
    if (out.empty()) return false;
    payload = std::move(out);
    return true;
}

compressor::api::AlgorithmID compress_id(bool use_flag) {
    return use_flag ? compressor::api::AlgorithmID::LZSS
                    : compressor::api::AlgorithmID::LZSS_NoFlag;
}

}  // namespace

int main() {
    using compressor::api::AlgorithmID;
    using compressor::api::compress;
    using compressor::api::compressFile;
    using compressor::api::decompress;
    using compressor::api::decompressFile;
    using compressor::api::unpack_wcx;
    using compressor::core::LZSSCompressor;

    const size_t sizes[] = {
        TestSizes::k64KB,
        TestSizes::k256KB,
        TestSizes::k512KB,
        TestSizes::k2MB,
    };
    const char* size_names[] = {"64KB", "256KB", "512KB", "2MB"};

    const fs::path work = fs::temp_directory_path() / "lzss_algorithm_new_matrix";
    std::error_code ec;
    fs::remove_all(work, ec);
    fs::create_directories(work, ec);

    int total = 0;
    int passed = 0;

    auto check = [&](bool ok, const char* label) {
        ++total;
        if (ok) {
            std::cout << "PASS " << label << "\n";
            ++passed;
        } else {
            std::cout << "FAIL " << label << "\n";
        }
        return ok;
    };

    for (int si = 0; si < 4; ++si) {
        const size_t data_size = sizes[si];
        const bool file_uses_streaming = data_size > TestSizes::kStreamingThreshold;
        std::cout << "\n=== LZSS " << size_names[si] << " (" << data_size << " bytes)"
                  << (file_uses_streaming ? ", compressFile streaming" : ", compressFile memory")
                  << " ===\n";

        const auto data = make_test_data(data_size);

        for (bool use_flag : {true, false}) {
            const char* flag = use_flag ? "flag_on" : "flag_off";
            const char* l_stream = use_flag ? "L1" : "L2";
            const char* l_mem = use_flag ? "L3" : "L4";
            const char* l_round = use_flag ? "L5" : "L6";

            LZSSCompressor comp;
            comp.set_use_flag_encoding(use_flag);

            const AlgorithmID cid = compress_id(use_flag);
            const AlgorithmID chain[] = {cid};

            // L3 / L4: 非流式（内存）压缩内容可解压且与原文件一致
            {
                const auto cr = comp.compress(data);
                const auto dec = comp.decompress(cr.data);
                check(dec.data == data,
                      (std::string(l_mem) + " memory round-trip " + flag).c_str());
            }

            // api::compress(bytes) 与 LZSSCompressor 输出一致（同 algorithm_new 路径）
            {
                const auto cr = comp.compress(data);
                const auto api_cr = compress(data, chain);
                check(api_cr.success && api_cr.data == cr.data,
                      (std::string(l_mem) + " api::compress == LZSSCompressor " + flag).c_str());
            }

            fs::path in = work / (std::string("in_") + flag + "_" + size_names[si] + ".bin");
            fs::path wcx = work / (std::string("out_") + flag + "_" + size_names[si] + ".wcx");
            fs::path dec_path = work / (std::string("dec_") + flag + "_" + size_names[si] + ".bin");
            write_all_bytes(in, data);

            // L1 / L2 + L5 / L6: 流式 compressFile / decompressFile 往返
            {
                const auto str_cr =
                    compressFile(in.string(), wcx.string(), chain, TestSizes::kStreamChunk);
                if (!check(str_cr.success,
                           (std::string(l_stream) + " compressFile " + flag).c_str())) {
                    continue;
                }
                const auto dr = decompressFile(
                    wcx.string(), dec_path.string(), chain, TestSizes::kStreamChunk);
                if (!check(dr.success,
                           (std::string(l_round) + " decompressFile " + flag).c_str())) {
                    continue;
                }
                const auto dec_data = read_all_bytes(dec_path);
                check(dec_data == data,
                      (std::string(l_round) + " stream round-trip bytes " + flag).c_str());
            }

            // 条件 A：内存压缩 payload 与 compressFile 解压出的 payload 字节一致或可互解到同一明文
            {
                const auto mem_cr = comp.compress(data);
                const auto wcx_data = read_all_bytes(wcx);
                const auto unpacked = unpack_wcx(wcx_data);
                if (!unpacked.success) {
                    check(false, (std::string("A unpack_wcx ") + flag).c_str());
                    continue;
                }
                auto stream_payload = unpacked.payload;
                strip_u32_framed(stream_payload);

                if (mem_cr.data == stream_payload) {
                    check(true, (std::string("A mem==stream identical ") + flag).c_str());
                } else {
                    const auto dec_mem = comp.decompress(mem_cr.data);
                    const auto dec_stream = comp.decompress(stream_payload);
                    check(dec_mem.data == data && dec_stream.data == data &&
                              dec_mem.data == dec_stream.data,
                          (std::string("A mem/stream decompress parity ") + flag).c_str());
                }
            }

            // 跨路径：流式 payload 用内存 decompressor 解；内存 payload 用 api::decompress 解
            {
                const auto mem_cr = comp.compress(data);
                const auto wcx_data = read_all_bytes(wcx);
                const auto unpacked = unpack_wcx(wcx_data);
                if (!unpacked.success) continue;

                auto stream_payload = unpacked.payload;
                strip_u32_framed(stream_payload);

                const auto dec_stream_via_mem = comp.decompress(stream_payload);
                check(dec_stream_via_mem.data == data,
                      (std::string("cross stream_payload->memory decompress ") + flag)
                          .c_str());

                const auto api_dec = decompress(mem_cr.data, chain);
                check(api_dec.success && api_dec.data == data,
                      (std::string("cross mem_payload->api::decompress ") + flag).c_str());
            }
        }
    }

    fs::remove_all(work, ec);
    std::cout << "\n========================================\n"
              << "  LZSS L1-L6 (algorithm_new): " << passed << "/" << total << " passed\n"
              << "========================================\n";
    return (passed == total) ? EXIT_SUCCESS : EXIT_FAILURE;
}

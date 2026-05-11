#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "api.hpp"

namespace fs = std::filesystem;

namespace {

void write_all_bytes(const fs::path& p, const std::vector<uint8_t>& data) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
}

}  // namespace

int main() {
    using compressor::api::AlgorithmID;
    using compressor::api::compressFile;
    using compressor::api::decompressFile;
    using compressor::api::unpack_wcx;

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path base =
        fs::temp_directory_path() / ("wcx_corrupt_" + std::to_string(stamp));
    std::error_code ec;
    fs::create_directories(base, ec);
    if (ec) {
        std::cerr << "create temp dir failed\n";
        return 1;
    }

    const std::vector<AlgorithmID> comp_chain{AlgorithmID::Deflate};
    const std::vector<AlgorithmID> decomp_chain{AlgorithmID::Inflate};

    const fs::path plain = base / "plain.bin";
    const fs::path wcx_ok = base / "ok.wcx";
    const fs::path wcx_trunc = base / "trunc.wcx";
    const fs::path wcx_bad_magic = base / "badmagic.wcx";
    const fs::path wcx_short_decl = base / "shortdecl.wcx";
    const fs::path out = base / "out.bin";

    std::vector<uint8_t> payload(2048);
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>(i * 17u);
    }
    write_all_bytes(plain, payload);

    auto comp = compressFile(plain.string(), wcx_ok.string(), comp_chain);
    if (!comp.success) {
        std::cerr << "compressFile failed: " << comp.error_message << '\n';
        fs::remove_all(base, ec);
        return 1;
    }

    std::ifstream read_ok(wcx_ok, std::ios::binary | std::ios::ate);
    if (!read_ok) {
        std::cerr << "open ok.wcx failed\n";
        fs::remove_all(base, ec);
        return 1;
    }
    const auto sz = static_cast<size_t>(read_ok.tellg());
    read_ok.seekg(0);
    std::vector<uint8_t> full_wcx(sz);
    read_ok.read(reinterpret_cast<char*>(full_wcx.data()),
                 static_cast<std::streamsize>(full_wcx.size()));

    auto hdr = unpack_wcx(full_wcx);
    if (!hdr.success || hdr.payload.empty()) {
        std::cerr << "unpack_wcx sanity failed\n";
        fs::remove_all(base, ec);
        return 1;
    }
    const size_t header_len = full_wcx.size() - hdr.payload.size();
    if (header_len < 18 || header_len > full_wcx.size()) {
        std::cerr << "bad header_len\n";
        fs::remove_all(base, ec);
        return 1;
    }

    const size_t cut =
        header_len + std::max<size_t>(1, hdr.payload.size() / 2);
    std::vector<uint8_t> truncated(full_wcx.begin(),
                                   full_wcx.begin() +
                                       static_cast<std::ptrdiff_t>(
                                           std::min(cut, full_wcx.size())));
    write_all_bytes(wcx_trunc, truncated);

    auto dec_trunc = decompressFile(wcx_trunc.string(), out.string(), decomp_chain);
    if (dec_trunc.success) {
        std::cerr << "truncated WCX must fail decompressFile\n";
        fs::remove_all(base, ec);
        return 1;
    }

    std::vector<uint8_t> badmagic = full_wcx;
    badmagic[0] = 'X';
    badmagic[1] = 'X';
    write_all_bytes(wcx_bad_magic, badmagic);
    auto dec_magic =
        decompressFile(wcx_bad_magic.string(), out.string(), decomp_chain);
    if (dec_magic.success) {
        std::cerr << "wrong magic must fail decompressFile\n";
        fs::remove_all(base, ec);
        return 1;
    }

    if (full_wcx.size() >= 14) {
        std::vector<uint8_t> huge_decl = full_wcx;
        huge_decl[10] = 0xFF;
        huge_decl[11] = 0xFF;
        huge_decl[12] = 0xFF;
        huge_decl[13] = 0x7F;
        write_all_bytes(wcx_short_decl, huge_decl);
        auto dec_short =
            decompressFile(wcx_short_decl.string(), out.string(), decomp_chain);
        if (dec_short.success) {
            std::cerr << "declared payload larger than file must fail\n";
            fs::remove_all(base, ec);
            return 1;
        }
    }

    auto dec_ok = decompressFile(wcx_ok.string(), out.string(), decomp_chain);
    if (!dec_ok.success) {
        std::cerr << "valid WCX roundtrip failed: " << dec_ok.error_message
                  << '\n';
        fs::remove_all(base, ec);
        return 1;
    }

    fs::remove_all(base, ec);
    std::cout << "[PASS] WCX corrupt inputs rejected; valid file still OK\n";
    return 0;
}

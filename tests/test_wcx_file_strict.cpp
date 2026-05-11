#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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
    f.read(reinterpret_cast<char*>(buf.data()),
           static_cast<std::streamsize>(buf.size()));
    return buf;
}

void write_all_bytes(const fs::path& p, const std::vector<uint8_t>& data) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(reinterpret_cast<const char*>(data.data()),
            static_cast<std::streamsize>(data.size()));
}

}  // namespace

int main() {
    using compressor::api::AlgorithmID;
    using compressor::api::compress;
    using compressor::api::compressFile;
    using compressor::api::decompressFile;

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path base =
        fs::temp_directory_path() / ("wcx_file_container_test_" + std::to_string(stamp));
    std::error_code ec;
    fs::create_directories(base, ec);
    if (ec) {
        std::cerr << "create temp dir failed\n";
        return 1;
    }

    const fs::path plain = base / "plain.bin";
    const fs::path wcx = base / "plain.wcx";
    const fs::path wcx_out = base / "plain.restore";
    const fs::path legacy_raw = base / "legacy.raw";
    const fs::path legacy_out = base / "legacy.restore";

    std::vector<uint8_t> payload;
    payload.reserve(4096);
    for (int i = 0; i < 512; ++i) {
        payload.push_back(static_cast<uint8_t>(i & 0xFF));
        payload.push_back(static_cast<uint8_t>((i * 7) & 0xFF));
        payload.push_back(static_cast<uint8_t>((i * 13) & 0xFF));
    }
    write_all_bytes(plain, payload);

    const std::vector<AlgorithmID> comp_chain{AlgorithmID::Deflate};
    const std::vector<AlgorithmID> decomp_chain{AlgorithmID::Inflate};

    auto wcx_comp = compressFile(plain.string(), wcx.string(), comp_chain);
    if (!wcx_comp.success) {
        std::cerr << "compressFile failed: " << wcx_comp.error_message << '\n';
        fs::remove_all(base, ec);
        return 1;
    }

    auto wcx_ok = decompressFile(wcx.string(), wcx_out.string(), decomp_chain);
    if (!wcx_ok.success || read_all_bytes(wcx_out) != payload) {
        std::cerr << "WCX file roundtrip failed\n";
        fs::remove_all(base, ec);
        return 1;
    }

    auto mem_comp = compress(payload, comp_chain);
    if (!mem_comp.success || mem_comp.data.empty()) {
        std::cerr << "compress(memory) failed: " << mem_comp.error_message << '\n';
        fs::remove_all(base, ec);
        return 1;
    }
    write_all_bytes(legacy_raw, mem_comp.data);

    auto raw_fail = decompressFile(legacy_raw.string(), legacy_out.string(), decomp_chain);
    if (raw_fail.success) {
        std::cerr << "non-WCX raw stream must be rejected\n";
        fs::remove_all(base, ec);
        return 1;
    }

    fs::remove_all(base, ec);
    std::cout << "[PASS] WCX file container only (non-WCX input rejected)\n";
    return 0;
}

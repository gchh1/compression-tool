#include <chrono>
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

void write_text(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
}

auto read_text(const fs::path& p) -> std::string {
    std::ifstream f(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

}  // namespace

int main() {
    using compressor::api::AlgorithmID;
    using compressor::api::compressDirectory;
    using compressor::api::compressFile;
    using compressor::api::decompressAndUnpackToDisk;
    using compressor::api::decompressFile;

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path base =
        fs::temp_directory_path() / ("wcx_strict_no_compat_" + std::to_string(stamp));
    std::error_code ec;
    fs::create_directories(base, ec);
    if (ec) {
        std::cerr << "create temp dir failed\n";
        return 1;
    }

    const std::vector<AlgorithmID> comp_chain{AlgorithmID::Deflate};
    const std::vector<AlgorithmID> decomp_chain{AlgorithmID::Inflate};

    std::vector<uint8_t> payload;
    payload.reserve(1024);
    for (int i = 0; i < 1024; ++i) payload.push_back(static_cast<uint8_t>(i & 0xFF));
    const fs::path file_in = base / "input.bin";
    const fs::path file_wcx = base / "input.wcx";
    const fs::path file_out = base / "output.bin";
    write_all_bytes(file_in, payload);

    auto file_comp = compressFile(file_in.string(), file_wcx.string(), comp_chain);
    if (!file_comp.success) {
        std::cerr << "compressFile failed: " << file_comp.error_message << '\n';
        fs::remove_all(base, ec);
        return 1;
    }
    auto file_decomp = decompressFile(file_wcx.string(), file_out.string(), decomp_chain);
    if (!file_decomp.success || read_all_bytes(file_out) != payload) {
        std::cerr << "WCX file roundtrip failed\n";
        fs::remove_all(base, ec);
        return 1;
    }

    const fs::path src_tree = base / "src_tree";
    write_text(src_tree / "a.txt", "alpha");
    write_text(src_tree / "nested" / "b.txt", "beta");
    const fs::path dir_wcx = base / "tree.wcx";
    const fs::path dir_out = base / "out_tree";
    auto dir_comp = compressDirectory(src_tree.string(), dir_wcx.string(), comp_chain);
    if (!dir_comp.success) {
        std::cerr << "compressDirectory failed: " << dir_comp.error_message << '\n';
        fs::remove_all(base, ec);
        return 1;
    }
    auto dir_decomp = decompressAndUnpackToDisk(dir_wcx.string(), dir_out.string());
    if (!dir_decomp.success) {
        std::cerr << "decompressAndUnpackToDisk failed: " << dir_decomp.error_message << '\n';
        fs::remove_all(base, ec);
        return 1;
    }
    if (read_text(dir_out / "a.txt") != "alpha" ||
        read_text(dir_out / "nested" / "b.txt") != "beta") {
        std::cerr << "WCX directory roundtrip mismatch\n";
        fs::remove_all(base, ec);
        return 1;
    }

    fs::remove_all(base, ec);
    std::cout << "[PASS] WCX-only decode paths (file + directory)\n";
    return 0;
}

#include <chrono>
#include <cstdint>
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
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
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
    using compressor::api::decompressAndUnpackToDisk;
    using compressor::api::unpack_wcx;

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path base =
        fs::temp_directory_path() / ("wcx_dir_archive_test_" + std::to_string(stamp));

    std::error_code ec;
    fs::create_directories(base, ec);
    if (ec) {
        std::cerr << "create temp base failed\n";
        return 1;
    }

    const fs::path src_tree = base / "src_tree";
    write_text(src_tree / "nested" / "hello.txt", "hello-wcx-dir");
    write_text(src_tree / "root.txt", "root-bytes");

    const fs::path archive = base / "bundle.wcx";
    const fs::path unpack_dir = base / "unpacked";

    const std::vector<AlgorithmID> chain{AlgorithmID::Deflate};
    auto pack_res =
        compressDirectory(src_tree.string(), archive.string(), chain);
    if (!pack_res.success) {
        std::cerr << "compressDirectory: " << pack_res.error_message << '\n';
        fs::remove_all(base, ec);
        return 1;
    }

    {
        std::ifstream in(archive, std::ios::binary);
        char magic[4]{};
        in.read(magic, 4);
        if (in.gcount() != 4 || std::string_view(magic, 4) != "WCMP") {
            std::cerr << "expected WCMP magic at archive start\n";
            fs::remove_all(base, ec);
            return 1;
        }
    }

    auto arc_bytes = read_all_bytes(archive);
    auto wcx = unpack_wcx(arc_bytes);
    if (!wcx.success) {
        std::cerr << "unpack_wcx: " << wcx.error_message << '\n';
        fs::remove_all(base, ec);
        return 1;
    }
    if (!wcx.is_folder) {
        std::cerr << "expected WCX folder flag\n";
        fs::remove_all(base, ec);
        return 1;
    }
    if (wcx.payload.empty()) {
        std::cerr << "empty inner pack payload\n";
        fs::remove_all(base, ec);
        return 1;
    }

    fs::create_directories(unpack_dir, ec);
    auto unpack_res =
        decompressAndUnpackToDisk(archive.string(), unpack_dir.string());
    if (!unpack_res.success) {
        std::cerr << "decompressAndUnpackToDisk: " << unpack_res.error_message
                  << '\n';
        fs::remove_all(base, ec);
        return 1;
    }

    if (read_text(unpack_dir / "nested" / "hello.txt") != "hello-wcx-dir") {
        std::cerr << "nested/hello.txt mismatch\n";
        fs::remove_all(base, ec);
        return 1;
    }
    if (read_text(unpack_dir / "root.txt") != "root-bytes") {
        std::cerr << "root.txt mismatch\n";
        fs::remove_all(base, ec);
        return 1;
    }

    fs::remove_all(base, ec);
    std::cout << "[PASS] Directory WCX archive roundtrip\n";
    return 0;
}

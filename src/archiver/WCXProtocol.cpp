#include "WCXProtocol.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <ios>
#include <ostream>

namespace compressor::archiver::wcx {

namespace {

constexpr std::array<uint8_t, 4> WCX_MAGIC{{'W', 'C', 'M', 'P'}};
constexpr uint8_t WCX_VERSION = 2;

auto writeU16LE(std::ostream& os, uint16_t v) -> void {
    uint8_t b[2] = {static_cast<uint8_t>(v & 0xFF),
                    static_cast<uint8_t>((v >> 8) & 0xFF)};
    os.write(reinterpret_cast<const char*>(b), 2);
}

auto writeU32LE(std::ostream& os, uint32_t v) -> void {
    uint8_t b[4] = {static_cast<uint8_t>(v & 0xFF),
                    static_cast<uint8_t>((v >> 8) & 0xFF),
                    static_cast<uint8_t>((v >> 16) & 0xFF),
                    static_cast<uint8_t>((v >> 24) & 0xFF)};
    os.write(reinterpret_cast<const char*>(b), 4);
}

auto readU16LE(const uint8_t* p) -> uint16_t {
    return static_cast<uint16_t>(p[0]) |
           (static_cast<uint16_t>(p[1]) << 8);
}

auto readU32LE(const uint8_t* p) -> uint32_t {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

auto toAlgoCode(core::AlgorithmID id) -> uint8_t {
    // WCX v2 single-byte algorithm id: keep in sync with
    // `src/gui/engine/file_protocol.py` ALGO_CODE_MAP.
    switch (id) {
        case core::AlgorithmID::None:
            return 0;
        case core::AlgorithmID::Deflate:
        case core::AlgorithmID::Inflate:
            return 1;
        case core::AlgorithmID::LZSS:
        case core::AlgorithmID::LZSSDecompress:
            return 2;
        case core::AlgorithmID::LZSS_NoFlag:
        case core::AlgorithmID::LZSSDecompress_NoFlag:
            return 9;
        case core::AlgorithmID::LZDP:
        case core::AlgorithmID::LZDPDecompress:
            return 3;
        case core::AlgorithmID::DPFlate:
            return 5;
        case core::AlgorithmID::Brotli:
        case core::AlgorithmID::BrotliDecompress:
            return 7;
        case core::AlgorithmID::Zstd:
        case core::AlgorithmID::ZstdDecompress:
            return 8;
        case core::AlgorithmID::DeltaEncode:
        case core::AlgorithmID::DeltaDecode:
        default:
            return 0;
    }
}

auto writeHeader(std::ofstream& output, uint8_t algo_code,
                 uint32_t original_size, uint32_t compressed_size,
                 const std::string& original_filename) -> bool {
    if (!output) return false;
    uint16_t fname_len = static_cast<uint16_t>(
        std::min<size_t>(original_filename.size(), UINT16_MAX));

    output.write(reinterpret_cast<const char*>(WCX_MAGIC.data()),
                 static_cast<std::streamsize>(WCX_MAGIC.size()));
    output.put(static_cast<char>(WCX_VERSION));
    output.put(static_cast<char>(algo_code));
    writeU32LE(output, original_size);
    writeU32LE(output, compressed_size);
    output.put(static_cast<char>(0));  // flags
    writeU16LE(output, fname_len);
    output.put(static_cast<char>(0));  // padding
    if (fname_len > 0) {
        output.write(original_filename.data(),
                     static_cast<std::streamsize>(fname_len));
    }
    return static_cast<bool>(output);
}

auto buildHeaderBytes(uint8_t algo_code, uint32_t original_size,
                      uint32_t compressed_size,
                      const std::string& original_filename) -> std::vector<uint8_t> {
    std::vector<uint8_t> out;
    uint16_t fname_len = static_cast<uint16_t>(
        std::min<size_t>(original_filename.size(), UINT16_MAX));
    out.reserve(FIXED_HEADER_SIZE + fname_len);
    out.insert(out.end(), WCX_MAGIC.begin(), WCX_MAGIC.end());
    out.push_back(WCX_VERSION);
    out.push_back(algo_code);
    out.push_back(static_cast<uint8_t>(original_size & 0xFF));
    out.push_back(static_cast<uint8_t>((original_size >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((original_size >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((original_size >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>(compressed_size & 0xFF));
    out.push_back(static_cast<uint8_t>((compressed_size >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((compressed_size >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((compressed_size >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>(0));  // flags
    out.push_back(static_cast<uint8_t>(fname_len & 0xFF));
    out.push_back(static_cast<uint8_t>((fname_len >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(0));  // padding
    if (fname_len > 0) {
        out.insert(out.end(), original_filename.begin(),
                   original_filename.begin() + fname_len);
    }
    return out;
}

auto patchCompressedSize(std::ofstream& output, uint32_t compressed_size)
    -> bool {
    if (!output) return false;
    output.seekp(10, std::ios::beg);  // offset of compressed_size
    writeU32LE(output, compressed_size);
    output.seekp(0, std::ios::end);
    return static_cast<bool>(output);
}

auto tryReadHeader(std::ifstream& input, HeaderView& out) -> bool {
    out = {};
    std::array<uint8_t, FIXED_HEADER_SIZE> fixed{};
    input.read(reinterpret_cast<char*>(fixed.data()),
               static_cast<std::streamsize>(fixed.size()));
    if (input.gcount() != static_cast<std::streamsize>(fixed.size())) {
        return false;
    }

    if (!std::equal(WCX_MAGIC.begin(), WCX_MAGIC.end(), fixed.begin())) {
        return false;
    }

    out.version = fixed[4];
    out.algo_code = fixed[5];
    out.original_size = readU32LE(fixed.data() + 6);
    out.compressed_size = readU32LE(fixed.data() + 10);
    out.flags = fixed[14];
    out.filename_len = readU16LE(fixed.data() + 15);
    out.padding = fixed[17];

    if (out.version != WCX_VERSION) return false;

    if (out.filename_len > 0) {
        std::string name(out.filename_len, '\0');
        input.read(name.data(), static_cast<std::streamsize>(out.filename_len));
        if (input.gcount() != static_cast<std::streamsize>(out.filename_len)) {
            return false;
        }
        out.original_filename = std::move(name);
    }

    out.total_size = FIXED_HEADER_SIZE + out.filename_len;
    out.valid = true;
    return true;
}

auto tryParseHeader(std::span<const uint8_t> data, HeaderView& out) -> bool {
    out = {};
    if (data.size() < FIXED_HEADER_SIZE) return false;
    auto fixed = data.first(FIXED_HEADER_SIZE);

    if (!std::equal(WCX_MAGIC.begin(), WCX_MAGIC.end(), fixed.begin())) {
        return false;
    }

    out.version = fixed[4];
    out.algo_code = fixed[5];
    out.original_size = readU32LE(fixed.data() + 6);
    out.compressed_size = readU32LE(fixed.data() + 10);
    out.flags = fixed[14];
    out.filename_len = readU16LE(fixed.data() + 15);
    out.padding = fixed[17];

    if (out.version != WCX_VERSION) return false;
    if (data.size() < FIXED_HEADER_SIZE + out.filename_len) return false;

    if (out.filename_len > 0) {
        const auto* p = reinterpret_cast<const char*>(data.data() + FIXED_HEADER_SIZE);
        out.original_filename.assign(p, p + out.filename_len);
    }
    out.total_size = FIXED_HEADER_SIZE + out.filename_len;
    out.valid = true;
    return true;
}

}  // namespace compressor::archiver::wcx


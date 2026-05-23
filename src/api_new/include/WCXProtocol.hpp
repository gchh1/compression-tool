#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <span>
#include <string>
#include <vector>

namespace compressor::core {
enum class AlgorithmID;
}

namespace compressor::api_new::wcx {

auto toAlgoCode(compressor::core::AlgorithmID id) -> uint8_t;

struct HeaderView {
    bool valid{false};
    uint8_t version{0};
    uint8_t algo_code{0};
    uint32_t original_size{0};
    uint32_t compressed_size{0};
    uint8_t flags{0};
    uint16_t filename_len{0};
    uint8_t padding{0};
    std::string original_filename;
    size_t total_size{0};
};

constexpr size_t FIXED_HEADER_SIZE = 18;

auto writeHeader(std::ofstream& output, uint8_t algo_code,
                 uint32_t original_size, uint32_t compressed_size,
                 const std::string& original_filename) -> bool;

auto patchCompressedSize(std::ofstream& output, uint32_t compressed_size)
    -> bool;

auto tryReadHeader(std::ifstream& input, HeaderView& out) -> bool;
auto tryParseHeader(std::span<const uint8_t> data, HeaderView& out) -> bool;

auto buildHeaderBytes(uint8_t algo_code, uint32_t original_size,
                      uint32_t compressed_size,
                      const std::string& original_filename) -> std::vector<uint8_t>;

}  // namespace compressor::api_new::wcx

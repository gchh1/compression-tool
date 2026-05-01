#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "AlgorithmFactory.hpp"

namespace compressor::api {

using AlgorithmID = core::AlgorithmID;

struct CompressResult {
    std::vector<uint8_t> data;
    size_t original_size{0};
    size_t compressed_size{0};
    bool success{false};
    std::string error_message;
};

struct WebFile {
    std::string name;
    std::vector<uint8_t> content;
};

/// Compress a single buffer with the given algorithm.
auto compress(const std::vector<uint8_t>& data, AlgorithmID algo)
    -> CompressResult;

/// Decompress a single buffer with the given algorithm.
auto decompress(const std::vector<uint8_t>& data, AlgorithmID algo)
    -> CompressResult;

/// Pack multiple files into a compressed archive.
auto packAndCompress(const std::vector<WebFile>& files, AlgorithmID algo)
    -> std::vector<uint8_t>;

/// Unpack a compressed archive back into individual files.
auto decompressAndUnpack(const std::vector<uint8_t>& data)
    -> std::vector<WebFile>;

}  // namespace compressor::api

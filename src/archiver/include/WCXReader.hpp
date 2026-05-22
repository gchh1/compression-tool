#pragma once

#ifndef __EMSCRIPTEN__

#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "api.hpp"

#include "WCXProtocol.hpp"

namespace compressor::archiver {

using compressor::api::CompressResult;

/// Single-file decompress: require a valid WCX header; return compressed payload
/// span length, or set `result.error_message` and return `nullopt`.
auto resolve_wcx_file_stream_payload_length(const std::string& input_path,
                                            std::ifstream& input,
                                            uint64_t file_on_disk_size,
                                            CompressResult& result)
    -> std::optional<uint64_t>;

}  // namespace compressor::archiver

#endif  // __EMSCRIPTEN__

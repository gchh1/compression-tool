#include "wcx_decompress_input.hpp"

#ifndef __EMSCRIPTEN__

#include "WCXProtocol.hpp"

namespace compressor::api {

auto resolve_wcx_file_stream_payload_length(const std::string& /*input_path*/,
                                            std::ifstream& input,
                                            uint64_t /*file_on_disk_size*/,
                                            CompressResult& result)
    -> std::optional<uint64_t> {
    wcx::HeaderView hdr{};
    if (wcx::tryReadHeader(input, hdr) && hdr.valid) {
        result.original_size = hdr.original_size;
        return hdr.compressed_size;
    }
    result.error_message = "Input is not a valid WCX container (WCMP header required)";
    return std::nullopt;
}

auto resolve_wcx_directory_archive_inner_pack(const std::string& /*input_path*/,
                                              const std::vector<uint8_t>& archive_data,
                                              CompressResult& result)
    -> std::optional<std::vector<uint8_t>> {
    auto wcx_result = unpack_wcx(archive_data);
    if (wcx_result.success) {
        return std::move(wcx_result.payload);
    }
    if (!wcx_result.error_message.empty()) {
        result.error_message = wcx_result.error_message;
    } else {
        result.error_message = "Input is not a valid WCX directory archive";
    }
    return std::nullopt;
}

}  // namespace compressor::api

#endif  // __EMSCRIPTEN__

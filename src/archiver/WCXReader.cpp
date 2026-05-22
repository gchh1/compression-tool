#include "WCXReader.hpp"

#ifndef __EMSCRIPTEN__

#include "WCXProtocol.hpp"

namespace compressor::archiver {

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

}  // namespace compressor::archiver

#endif  // __EMSCRIPTEN__

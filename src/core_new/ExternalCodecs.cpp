#include "GzipCompressor.hpp"

#include <chrono>
#include <cstring>
#include <vector>

#include <zlib.h>

namespace compressor::core {

namespace {

CompressorResult fail_codec(const std::string& msg, size_t in_size) {
    CompressorResult r;
    r.success = false;
    r.error_message = msg;
    r.original_size = in_size;
    return r;
}

}  // namespace

CompressorResult GzipCompressor::compress(std::vector<uint8_t> data) {
    CompressorResult result;
    const auto t0 = std::chrono::high_resolution_clock::now();

    z_stream strm{};
    int level = static_cast<int>(compression_level_);
    if (level < 0) {
        level = 0;
    }
    if (level > 9) {
        level = 9;
    }

    const int ret_init =
        deflateInit2(&strm, level, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
    if (ret_init != Z_OK) {
        return fail_codec("deflateInit2 failed", data.size());
    }

    strm.next_in = reinterpret_cast<Bytef*>(data.data());
    strm.avail_in = static_cast<uInt>(data.size());

    const size_t out_size = deflateBound(&strm, static_cast<uLong>(data.size()));
    std::vector<uint8_t> out(out_size);
    strm.next_out = out.data();
    strm.avail_out = static_cast<uInt>(out_size);

    const int ret = deflate(&strm, Z_FINISH);
    deflateEnd(&strm);

    if (ret != Z_STREAM_END) {
        return fail_codec("deflate failed", data.size());
    }

    out.resize(strm.total_out);
    result.data = std::move(out);
    result.original_size = data.size();
    result.compressed_size = result.data.size();
    result.compression_ratio =
        result.original_size > 0
            ? static_cast<double>(result.compressed_size) / result.original_size
            : 0.0;
    result.time_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - t0)
            .count();
    result.success = true;
    return result;
}

CompressorResult GzipCompressor::decompress(std::vector<uint8_t> data) {
    CompressorResult result;
    const auto t0 = std::chrono::high_resolution_clock::now();

    z_stream strm{};
    if (inflateInit2(&strm, 15 + 16) != Z_OK) {
        return fail_codec("inflateInit2 failed", data.size());
    }

    size_t out_capacity = std::max(data.size() * 4, size_t{4096});
    std::vector<uint8_t> out(out_capacity);

    strm.next_in = reinterpret_cast<Bytef*>(data.data());
    strm.avail_in = static_cast<uInt>(data.size());

    int ret = Z_OK;
    while (ret != Z_STREAM_END) {
        if (strm.avail_out == 0) {
            const size_t old_size = out.size();
            out.resize(old_size * 2);
            strm.next_out = out.data() + old_size;
            strm.avail_out = static_cast<uInt>(old_size);
        }
        strm.next_out = out.data() + strm.total_out;
        strm.avail_out = static_cast<uInt>(out.size() - strm.total_out);
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            inflateEnd(&strm);
            return fail_codec("inflate failed", data.size());
        }
    }

    inflateEnd(&strm);
    out.resize(strm.total_out);
    result.data = std::move(out);
    result.original_size = data.size();
    result.compressed_size = result.data.size();
    result.time_ms =
        std::chrono::duration<double, std::milli>(
            std::chrono::high_resolution_clock::now() - t0)
            .count();
    result.success = true;
    return result;
}

std::string GzipCompressor::get_algorithm_name() { return "Gzip (zlib)"; }

CompressorResult BrotliCompressor::compress(std::vector<uint8_t> data) {
    return fail_codec("Brotli: not available in algorithm_new api stack", data.size());
}

CompressorResult BrotliCompressor::decompress(std::vector<uint8_t> data) {
    return fail_codec("Brotli: not available in algorithm_new api stack", data.size());
}

std::string BrotliCompressor::get_algorithm_name() { return "Brotli (unavailable)"; }

CompressorResult ZstdCompressor::compress(std::vector<uint8_t> data) {
    return fail_codec("Zstd: not available in algorithm_new api stack", data.size());
}

CompressorResult ZstdCompressor::decompress(std::vector<uint8_t> data) {
    return fail_codec("Zstd: not available in algorithm_new api stack", data.size());
}

std::string ZstdCompressor::get_algorithm_name() { return "Zstd (unavailable)"; }

}  // namespace compressor::core

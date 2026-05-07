#include "GzipCompressor.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include <zlib.h>

namespace compressor {
namespace core {

auto GzipCompressor::compress(std::vector<uint8_t> data) -> CompressorResult {
    CompressorResult result;
    auto start_time = std::chrono::high_resolution_clock::now();

    z_stream strm{};
    int level = static_cast<int>(compression_level_);
    if (level < 0) level = 0;
    if (level > 9) level = 9;

    int ret = deflateInit2(&strm, level, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
    if (ret != Z_OK) {
        result.success = false;
        result.error_message = "deflateInit2 failed";
        return result;
    }

    strm.next_in = reinterpret_cast<Bytef*>(data.data());
    strm.avail_in = static_cast<uInt>(data.size());

    size_t out_size = deflateBound(&strm, static_cast<uLong>(data.size()));
    std::vector<uint8_t> out(out_size);
    strm.next_out = out.data();
    strm.avail_out = static_cast<uInt>(out_size);

    ret = deflate(&strm, Z_FINISH);
    deflateEnd(&strm);

    if (ret != Z_STREAM_END) {
        result.success = false;
        result.error_message = "deflate failed";
        return result;
    }

    out.resize(strm.total_out);
    result.data = std::move(out);

    auto end_time = std::chrono::high_resolution_clock::now();
    result.original_size = data.size();
    result.compressed_size = result.data.size();
    if (result.original_size > 0) {
        result.compression_ratio =
            static_cast<double>(result.compressed_size) / result.original_size;
    }
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
    result.time_ms = elapsed.count();
    result.success = true;

    return result;
}

auto GzipCompressor::decompress(std::vector<uint8_t> data) -> CompressorResult {
    CompressorResult result;
    auto start_time = std::chrono::high_resolution_clock::now();

    z_stream strm{};
    int ret = inflateInit2(&strm, 15 + 16);
    if (ret != Z_OK) {
        result.success = false;
        result.error_message = "inflateInit2 failed";
        return result;
    }

    size_t out_capacity = data.size() * 4;
    if (out_capacity < 4096) out_capacity = 4096;
    std::vector<uint8_t> out(out_capacity);

    strm.next_in = reinterpret_cast<Bytef*>(data.data());
    strm.avail_in = static_cast<uInt>(data.size());

    do {
        if (strm.avail_out == 0) {
            size_t old_size = out.size();
            out.resize(old_size * 2);
            strm.next_out = out.data() + old_size;
            strm.avail_out = static_cast<uInt>(old_size);
        }

        strm.next_out = out.data() + strm.total_out;
        strm.avail_out = static_cast<uInt>(out.size() - strm.total_out);

        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR || ret == Z_MEM_ERROR) {
            inflateEnd(&strm);
            result.success = false;
            result.error_message = "inflate failed";
            return result;
        }
    } while (ret != Z_STREAM_END);

    inflateEnd(&strm);
    out.resize(strm.total_out);
    result.data = std::move(out);

    auto end_time = std::chrono::high_resolution_clock::now();
    result.original_size = data.size();
    result.compressed_size = result.data.size();
    std::chrono::duration<double, std::milli> elapsed = end_time - start_time;
    result.time_ms = elapsed.count();
    result.success = true;

    return result;
}

} // namespace core
} // namespace compressor

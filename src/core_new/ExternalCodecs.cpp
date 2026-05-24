#include "GzipCompressor.hpp"
#include "ImageCompressorBindings.hpp"

#include <chrono>
#include <cstring>
#include <vector>

#include <zlib.h>
#include "Brotli.hpp"
#include "ImageCompressor.hpp"
#include "Zstd.hpp"

namespace compressor::core {

namespace {

CompressorResult ok_codec(std::vector<uint8_t> data, size_t original_size,
                          double time_ms) {
    CompressorResult r;
    r.success = true;
    r.original_size = original_size;
    r.compressed_size = data.size();
    r.compression_ratio =
        original_size > 0
            ? static_cast<double>(data.size()) / original_size
            : 0.0;
    r.time_ms = time_ms;
    r.data = std::move(data);
    return r;
}

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
    const auto t0 = std::chrono::high_resolution_clock::now();

    algorithm::BrotliParams params;
    params.window_size = window_size_;
    params.min_match = min_match_ == 0 ? 4 : min_match_;
    params.max_chain_length = max_chain_length_;

    auto result = algorithm::brotli_encode(data, params);
    if (result.empty() && !data.empty()) {
        return fail_codec("Brotli encode failed", data.size());
    }

    const auto t1 = std::chrono::high_resolution_clock::now();
    return ok_codec(std::move(result), data.size(),
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
}

CompressorResult BrotliCompressor::decompress(std::vector<uint8_t> data) {
    const auto t0 = std::chrono::high_resolution_clock::now();

    auto result = algorithm::brotli_decode(data);
    if (result.empty() && !data.empty()) {
        return fail_codec("Brotli decode failed", data.size());
    }

    const auto t1 = std::chrono::high_resolution_clock::now();
    return ok_codec(std::move(result), data.size(),
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
}

std::string BrotliCompressor::get_algorithm_name() { return "Brotli"; }

CompressorResult ZstdCompressor::compress(std::vector<uint8_t> data) {
    const auto t0 = std::chrono::high_resolution_clock::now();

    auto result = algorithm::zstd_compress(data, static_cast<int>(compression_level_));
    if (result.empty() && !data.empty()) {
        return fail_codec("Zstd compress failed", data.size());
    }

    const auto t1 = std::chrono::high_resolution_clock::now();
    return ok_codec(std::move(result), data.size(),
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
}

CompressorResult ZstdCompressor::decompress(std::vector<uint8_t> data) {
    const auto t0 = std::chrono::high_resolution_clock::now();

    auto result = algorithm::zstd_decompress(data);
    if (result.empty() && !data.empty()) {
        return fail_codec("Zstd decompress failed", data.size());
    }

    const auto t1 = std::chrono::high_resolution_clock::now();
    return ok_codec(std::move(result), data.size(),
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
}

std::string ZstdCompressor::get_algorithm_name() { return "Zstd"; }

CompressorResult ImageJpegCompressor::compress(std::vector<uint8_t> data) {
    const auto t0 = std::chrono::high_resolution_clock::now();

    auto result = algorithm::image_compress(data, algorithm::ImageFormat::JPEG, quality_);
    if (result.empty() && !data.empty()) {
        return fail_codec("JPEG compress failed", data.size());
    }

    const auto t1 = std::chrono::high_resolution_clock::now();
    return ok_codec(std::move(result), data.size(),
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
}

CompressorResult ImageJpegCompressor::decompress(std::vector<uint8_t> data) {
    const auto t0 = std::chrono::high_resolution_clock::now();

    auto result = algorithm::image_decompress(data);
    if (result.empty() && !data.empty()) {
        return fail_codec("JPEG decompress failed", data.size());
    }

    const auto t1 = std::chrono::high_resolution_clock::now();
    return ok_codec(std::move(result), data.size(),
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
}

std::string ImageJpegCompressor::get_algorithm_name() { return "JPEG"; }

CompressorResult ImagePngCompressor::compress(std::vector<uint8_t> data) {
    const auto t0 = std::chrono::high_resolution_clock::now();

    auto result = algorithm::image_compress(data, algorithm::ImageFormat::PNG);
    if (result.empty() && !data.empty()) {
        return fail_codec("PNG compress failed", data.size());
    }

    const auto t1 = std::chrono::high_resolution_clock::now();
    return ok_codec(std::move(result), data.size(),
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
}

CompressorResult ImagePngCompressor::decompress(std::vector<uint8_t> data) {
    const auto t0 = std::chrono::high_resolution_clock::now();

    auto result = algorithm::image_decompress(data);
    if (result.empty() && !data.empty()) {
        return fail_codec("PNG decompress failed", data.size());
    }

    const auto t1 = std::chrono::high_resolution_clock::now();
    return ok_codec(std::move(result), data.size(),
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
}

std::string ImagePngCompressor::get_algorithm_name() { return "PNG"; }

}  // namespace compressor::core

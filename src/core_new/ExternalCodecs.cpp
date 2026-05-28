#include "GzipCompressor.hpp"
#include "ImageCompressorBindings.hpp"
#include "AudioCompressorBindings.hpp"
#include "VideoCompressorBindings.hpp"

#include <chrono>
#include <cstring>
#include <vector>

#include <zlib.h>
#include "AudioCodec.hpp"
#include "Brotli.hpp"
#include "ImageCompressor.hpp"
#include "VideoCodec.hpp"
#include "OpenH264Codec.hpp"
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

// ── Audio FLAC ──

CompressorResult AudioFlacCompressor::compress(std::vector<uint8_t> data) {
    auto t0 = std::chrono::high_resolution_clock::now();

    // Detect WAV data: check for "RIFF" header
    bool is_wav = (data.size() > 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F');

    std::vector<uint8_t> result;
    if (is_wav) {
        // Parse WAV and re-encode as FLAC
        auto wav = algorithm::parse_wav(data);
        if (wav.sample_rate == 0)
            return fail_codec("Failed to parse WAV file", 0);

        // Extract raw PCM samples from WAV data
        size_t sample_count = wav.data_size / (wav.bits_per_sample / 8);
        std::vector<int16_t> samples(sample_count);
        for (size_t i = 0; i < sample_count; ++i) {
            if (wav.bits_per_sample == 16) {
                int16_t val;
                std::memcpy(&val, data.data() + wav.data_offset + i * 2, 2);
                samples[i] = val;
            } else if (wav.bits_per_sample == 8) {
                samples[i] = static_cast<int16_t>(
                    (static_cast<int>(data[wav.data_offset + i]) - 128) << 8);
            }
        }

        result = algorithm::flac::encode(samples, wav.sample_rate,
                                          wav.num_channels, wav.bits_per_sample,
                                          quality_);
    } else {
        // Raw audio: assume 44.1kHz 16-bit stereo
        size_t sample_count = data.size() / 2;
        std::vector<int16_t> samples(sample_count);
        for (size_t i = 0; i < sample_count; ++i) {
            int16_t val;
            std::memcpy(&val, data.data() + i * 2, 2);
            samples[i] = val;
        }
        result = algorithm::flac::encode(samples, 44100, 2, 16, quality_);
    }

    const auto t1 = std::chrono::high_resolution_clock::now();
    return ok_codec(std::move(result), data.size(),
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
}

CompressorResult AudioFlacCompressor::decompress(std::vector<uint8_t> data) {
    auto t0 = std::chrono::high_resolution_clock::now();
    // FLAC decode not yet implemented
    (void)data;
    const auto t1 = std::chrono::high_resolution_clock::now();
    return fail_codec("FLAC decode not implemented",
                       0);
}

std::string AudioFlacCompressor::get_algorithm_name() { return "FLAC"; }

// ── Audio AAC ──

CompressorResult AudioAacCompressor::compress(std::vector<uint8_t> data) {
    auto t0 = std::chrono::high_resolution_clock::now();

    // Detect WAV data: check for "RIFF" header
    bool is_wav = (data.size() > 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F');

    std::vector<uint8_t> result;
    if (is_wav) {
        auto wav = algorithm::parse_wav(data);
        if (wav.sample_rate == 0)
            return fail_codec("Failed to parse WAV file", 0);

        size_t sample_count = wav.data_size / (wav.bits_per_sample / 8);
        std::vector<int16_t> samples(sample_count);
        for (size_t i = 0; i < sample_count; ++i) {
            if (wav.bits_per_sample == 16) {
                int16_t val;
                std::memcpy(&val, data.data() + wav.data_offset + i * 2, 2);
                samples[i] = val;
            } else if (wav.bits_per_sample == 8) {
                samples[i] = static_cast<int16_t>(
                    (static_cast<int>(data[wav.data_offset + i]) - 128) << 8);
            }
        }

        int bitrate = 64 + quality_ * 32; // quality 0-8 → 64-320 kbps
        result = algorithm::aac::encode(samples, wav.sample_rate,
                                         wav.num_channels, wav.bits_per_sample,
                                         bitrate);
    } else {
        size_t sample_count = data.size() / 2;
        std::vector<int16_t> samples(sample_count);
        if (!samples.empty())
            std::memcpy(samples.data(), data.data(), data.size());

        int bitrate = 64 + quality_ * 32;
        result = algorithm::aac::encode(samples, 44100, 2, 16, bitrate);
    }

    if (result.empty())
        return fail_codec("AAC encode failed", data.size());

    const auto t1 = std::chrono::high_resolution_clock::now();
    return ok_codec(std::move(result), data.size(),
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
}

CompressorResult AudioAacCompressor::decompress(std::vector<uint8_t> data) {
    auto t0 = std::chrono::high_resolution_clock::now();
    auto result = algorithm::aac::decode(data);
    if (result.empty())
        return fail_codec("AAC decode not yet implemented", data.size());
    const auto t1 = std::chrono::high_resolution_clock::now();

    int sample_rate = 44100;
    int num_channels = 2;
    if (data.size() >= 7) {
        int sr_idx = (data[2] >> 2) & 0x0F;
        sample_rate = [](int idx) -> int {
            switch (idx) {
                case 0:  return 96000; case 1:  return 88200;
                case 2:  return 64000; case 3:  return 48000;
                case 4:  return 44100; case 5:  return 32000;
                case 6:  return 24000; case 7:  return 22050;
                case 8:  return 16000; case 9:  return 12000;
                case 10: return 11025; case 11: return 8000;
                default: return 44100;
            }
        }(sr_idx);
        num_channels = ((data[2] & 0x01) << 2) | ((data[3] >> 6) & 0x03);
        if (num_channels == 0) num_channels = 1;
    }

    auto wav = algorithm::build_wav(result, sample_rate, num_channels, 16);
    return ok_codec(std::move(wav), data.size(),
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
}

std::string AudioAacCompressor::get_algorithm_name() { return "AAC-LC"; }

// ── Video H.264 ──

CompressorResult VideoH264Compressor::compress(std::vector<uint8_t> data) {
    auto t0 = std::chrono::high_resolution_clock::now();
    auto result = algorithm::video_compress(data, algorithm::VideoFormat::H264, quality_);
    if (result.empty())
        return fail_codec("H.264 仅支持原始 RGB24 视频（16字节头+w*h*3每帧），不支持 MP4/MKV/AVI 容器格式", data.size());
    const auto t1 = std::chrono::high_resolution_clock::now();
    return ok_codec(std::move(result), data.size(),
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
}

CompressorResult VideoH264Compressor::decompress(std::vector<uint8_t> data) {
    auto t0 = std::chrono::high_resolution_clock::now();
    auto result = algorithm::video_decompress(data);
    if (result.empty())
        return fail_codec("H.264 decode not yet implemented", data.size());
    const auto t1 = std::chrono::high_resolution_clock::now();
    return ok_codec(std::move(result), data.size(),
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
}

std::string VideoH264Compressor::get_algorithm_name() { return "H.264"; }

// ── Video OpenH264 ──

CompressorResult VideoOpenH264Compressor::compress(std::vector<uint8_t> data) {
    auto t0 = std::chrono::high_resolution_clock::now();
    auto result = algorithm::openh264_compress(data, quality_);
    if (result.empty())
        return fail_codec("OpenH264 仅支持原始 RGB24 视频（16字节头+w*h*3每帧），不支持 MP4/MKV/AVI 容器格式", data.size());
    const auto t1 = std::chrono::high_resolution_clock::now();
    return ok_codec(std::move(result), data.size(),
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
}

CompressorResult VideoOpenH264Compressor::decompress(std::vector<uint8_t> data) {
    auto t0 = std::chrono::high_resolution_clock::now();
    auto result = algorithm::openh264_decompress(data);
    if (result.empty())
        return fail_codec("OpenH264 decode failed", data.size());
    const auto t1 = std::chrono::high_resolution_clock::now();
    return ok_codec(std::move(result), data.size(),
                    std::chrono::duration<double, std::milli>(t1 - t0).count());
}

std::string VideoOpenH264Compressor::get_algorithm_name() { return "OpenH264"; }

}  // namespace compressor::core

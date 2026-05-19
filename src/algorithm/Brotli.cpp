#include "Brotli.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <brotli/decode.h>
#include <brotli/encode.h>

namespace compressor::algorithm {
namespace {

auto lgwin_from_window(size_t window_size) -> int {
    int lg = 10;
    while ((static_cast<size_t>(1) << lg) < window_size && lg < 24) {
        ++lg;
    }
    return lg;
}

auto quality_from_params(const BrotliParams& params) -> int {
    // Legacy GUI ``max_chain_length`` mapped to Brotli quality 1..11 (default 6).
    int q = static_cast<int>(params.max_chain_length / 128);
    if (q < 1) {
        q = 6;
    }
    if (q > 11) {
        q = 11;
    }
    return q;
}

}  // namespace

auto brotli_encode(const std::vector<uint8_t>& data, const BrotliParams& params)
    -> std::vector<uint8_t> {
    if (data.empty()) {
        return {};
    }

    const int quality = quality_from_params(params);
    const int lgwin = lgwin_from_window(params.window_size);

    size_t encoded_cap = BrotliEncoderMaxCompressedSize(data.size());
    if (encoded_cap == 0) {
        return {};
    }
    std::vector<uint8_t> out(encoded_cap);

    size_t encoded_size = encoded_cap;
    const BROTLI_BOOL ok = BrotliEncoderCompress(
        quality, lgwin, BROTLI_MODE_TEXT, data.size(), data.data(), &encoded_size,
        out.data());
    if (ok != BROTLI_TRUE) {
        return {};
    }
    out.resize(encoded_size);
    return out;
}

auto brotli_decode(const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
    if (data.empty()) {
        return {};
    }

    size_t decoded_cap = std::max(data.size() * 4 + 65536, size_t{65536});
    for (int attempt = 0; attempt < 12; ++attempt) {
        std::vector<uint8_t> out(decoded_cap);
        size_t decoded_size = decoded_cap;
        const BrotliDecoderResult res = BrotliDecoderDecompress(
            data.size(), data.data(), &decoded_size, out.data());
        if (res == BROTLI_DECODER_RESULT_SUCCESS) {
            out.resize(decoded_size);
            return out;
        }
        if (res == BROTLI_DECODER_RESULT_NEEDS_MORE_OUTPUT) {
            decoded_cap = std::max(decoded_size + 65536, decoded_cap * 2);
            continue;
        }
        return {};
    }
    return {};
}

BrotliCompress::BrotliCompress(size_t window_size, size_t min_match,
                               size_t max_chain_length)
    : params_{window_size, min_match, max_chain_length} {
    reset();
}

auto BrotliCompress::reset() -> void {
    input_buffer_.clear();
    finished_ = false;
}

auto BrotliCompress::handle(AlgorithmStatus& status, bool is_last_chunk) -> void {
    if (finished_) {
        status.done = true;
        return;
    }

    const size_t remain = reader_.getRemainSize();
    if (remain > 0) {
        std::vector<uint8_t> chunk(remain);
        reader_.readBytes(chunk.data(), remain);
        input_buffer_.insert(input_buffer_.end(), chunk.begin(), chunk.end());
    }

    if (!is_last_chunk) {
        status.need_input = true;
        return;
    }

    const auto encoded = brotli_encode(input_buffer_, params_);
    writer_.writeBytes(encoded.data(), encoded.size());
    finished_ = true;
    status.done = true;
}

BrotliDecompress::BrotliDecompress() {
    reset();
}

auto BrotliDecompress::reset() -> void {
    input_buffer_.clear();
    finished_ = false;
}

auto BrotliDecompress::handle(AlgorithmStatus& status, bool is_last_chunk) -> void {
    if (finished_) {
        status.done = true;
        return;
    }

    const size_t remain = reader_.getRemainSize();
    if (remain > 0) {
        std::vector<uint8_t> chunk(remain);
        reader_.readBytes(chunk.data(), remain);
        input_buffer_.insert(input_buffer_.end(), chunk.begin(), chunk.end());
    }

    if (!is_last_chunk) {
        status.need_input = true;
        return;
    }

    const auto decoded = brotli_decode(input_buffer_);
    writer_.writeBytes(decoded.data(), decoded.size());
    finished_ = true;
    status.done = true;
}

}  // namespace compressor::algorithm

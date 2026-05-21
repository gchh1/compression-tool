#include "ImageCompressor.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_WARN_MISSING_IMPL
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace compressor::algorithm {

namespace {

struct WriteContext {
    std::vector<uint8_t> buffer;
};

void writeCb(void* context, void* data, int size) {
    auto* ctx = static_cast<WriteContext*>(context);
    auto* bytes = static_cast<uint8_t*>(data);
    ctx->buffer.insert(ctx->buffer.end(), bytes, bytes + size);
}

auto encodeJpeg(const uint8_t* pixels, int w, int h, int ch,
                int quality) -> std::vector<uint8_t> {
    WriteContext ctx;
    stbi_write_jpg_to_func(writeCb, &ctx, w, h, ch, pixels, quality);
    return std::move(ctx.buffer);
}

auto encodePng(const uint8_t* pixels, int w, int h, int ch,
               int /*quality*/) -> std::vector<uint8_t> {
    WriteContext ctx;
    stbi_write_png_to_func(writeCb, &ctx, w, h, ch, pixels, w * ch);
    return std::move(ctx.buffer);
}

}  // namespace

auto ImageCompressor::process(std::span<const uint8_t> read,
                              std::span<uint8_t> write,
                              bool is_last_chunk) -> AlgorithmStatus {
    AlgorithmStatus status;

    if (!read.empty()) {
        accumulator_.insert(accumulator_.end(), read.begin(), read.end());
    }
    status.bytes_consumed = read.size();

    if (!finished_ && is_last_chunk) {
        int w = 0, h = 0, c = 0;
        auto* pixels = stbi_load_from_memory(
            accumulator_.data(), static_cast<int>(accumulator_.size()),
            &w, &h, &c, 0);

        if (pixels) {
            int out_c = (format_ == ImageFormat::JPEG) ? 3 : c;
            switch (format_) {
                case ImageFormat::JPEG:
                    output_buffer_ = encodeJpeg(pixels, w, h, c, quality_);
                    break;
                case ImageFormat::PNG:
                    output_buffer_ = encodePng(pixels, w, h, c, quality_);
                    break;
            }
            stbi_image_free(pixels);
        } else {
            // Decode failed — passthrough original bytes
            output_buffer_ = std::move(accumulator_);
        }
        finished_ = true;
    }

    status.bytes_produced = copyOutput(write);
    status.done = finished_ && output_pos_ >= output_buffer_.size();
    status.need_input = !finished_ && !is_last_chunk;
    status.need_output = output_pos_ < output_buffer_.size();

    return status;
}

auto ImageCompressor::reset() -> void {
    accumulator_.clear();
    output_buffer_.clear();
    output_pos_ = 0;
    finished_ = false;
}

auto ImageCompressor::copyOutput(std::span<uint8_t> write) -> size_t {
    size_t available = output_buffer_.size() - output_pos_;
    size_t n = std::min(available, write.size());
    if (n > 0) {
        std::memcpy(write.data(), output_buffer_.data() + output_pos_, n);
        output_pos_ += n;
        if (output_pos_ >= output_buffer_.size()) {
            output_buffer_.clear();
            output_pos_ = 0;
        }
    }
    return n;
}

}  // namespace compressor::algorithm

#include "ImageCompressor.hpp"

#include <algorithm>
#include <cstring>

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

auto encodePng(const uint8_t* pixels, int w, int h, int ch) -> std::vector<uint8_t> {
    WriteContext ctx;
    stbi_write_png_to_func(writeCb, &ctx, w, h, ch, pixels, w * ch);
    return std::move(ctx.buffer);
}

auto decodeImage(const uint8_t* data, int size) -> std::vector<uint8_t> {
    int w = 0, h = 0, c = 0;
    auto* pixels = stbi_load_from_memory(data, size, &w, &h, &c, 0);
    if (!pixels) {
        return {};
    }
    std::vector<uint8_t> result(pixels, pixels + static_cast<size_t>(w * h * c));
    stbi_image_free(pixels);
    return result;
}

}  // namespace

auto image_compress(const std::vector<uint8_t>& data,
                    ImageFormat format,
                    int quality) -> std::vector<uint8_t> {
    int w = 0, h = 0, c = 0;
    auto* pixels = stbi_load_from_memory(
        data.data(), static_cast<int>(data.size()), &w, &h, &c, 0);

    if (!pixels) {
        return {};
    }

    std::vector<uint8_t> result;
    switch (format) {
        case ImageFormat::JPEG:
            result = encodeJpeg(pixels, w, h, c, quality);
            break;
        case ImageFormat::PNG:
            result = encodePng(pixels, w, h, c);
            break;
    }
    stbi_image_free(pixels);
    return result;
}

auto image_decompress(const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
    return decodeImage(data.data(), static_cast<int>(data.size()));
}

}  // namespace compressor::algorithm
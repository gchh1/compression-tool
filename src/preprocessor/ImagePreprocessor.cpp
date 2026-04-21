/**
 * @file ImagePreprocessor.cpp
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2026-04-19
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "ImagePreprocessor.hpp"

#include <cstddef>
#include <stdexcept>
#include <vector>

#include "Delta.hpp"
#include "ImageParser.hpp"

namespace compressor {

using algorithm::Delta;

namespace core {
/**
 * @brief Encode raw image data through Delta algorithm
 *
 * @param raw_data
 * @param quality
 * @return std::vector<uint8_t>
 */
auto ImagePreprocessor::encode(std::vector<uint8_t> raw_data, int quality)
    -> std::vector<uint8_t> {
    /* 1. Parse the raw context data to pixel form*/
    ImageData img = ImageParser::parse(std::move(raw_data));

    if (!img.success) {
        throw std::runtime_error("");
    }

    /* 2. Push the width and height data */
    // We combine width and height to 8 bytes, the high 4 bytes represents width
    // while the low 4 bytes represents height
    uint8_t header[8];
    header[0] = img.width >> 24;
    header[1] = img.width >> 16;
    header[2] = img.width >> 8;
    header[3] = img.width;
    header[4] = img.height >> 24;
    header[5] = img.height >> 16;
    header[6] = img.height >> 8;
    header[7] = img.height;

    /* 3. Insert Delta data */
    auto delta_context = Delta::encode(std::move(img.pixels), quality);
    delta_context.insert(delta_context.begin(), header, header + 8);

    return delta_context;
}

/**
 * @brief Decode through supporting decode algorithm
 *
 * @param preprocessed_data
 * @return std::vector<uint8_t>
 */
auto ImagePreprocessor::decode(std::vector<uint8_t> preprocessed_data)
    -> std::vector<uint8_t> {
    size_t idx = 0;
    /* 1. Get the width and height */
    int width = static_cast<uint32_t>(preprocessed_data[idx]) << 24 |
                static_cast<uint32_t>(preprocessed_data[idx + 1]) << 16 |
                static_cast<uint32_t>(preprocessed_data[idx + 2]) << 8 |
                static_cast<uint32_t>(preprocessed_data[idx + 3]);
    idx += 4;

    int height = static_cast<uint32_t>(preprocessed_data[idx]) << 24 |
                 static_cast<uint32_t>(preprocessed_data[idx + 1]) << 16 |
                 static_cast<uint32_t>(preprocessed_data[idx + 2]) << 8 |
                 static_cast<uint32_t>(preprocessed_data[idx + 3]);
    idx += 4;

    /* 2. Delta decode */
    preprocessed_data.erase(preprocessed_data.begin(),
                            preprocessed_data.begin() + idx);
    auto delta_decode_context = Delta::decode(std::move(preprocessed_data));

    /* 3. Restore to .png */
    return ImageParser::restore(width, height, std::move(delta_decode_context));
}

}  // namespace core

}  // namespace compressor
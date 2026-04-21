
#include "ImageParser.hpp"

#include <cstdint>
#include <vector>

#include "stb_image.h"
#include "stb_image_write.h"

namespace compressor {
namespace core {
/**
 * @brief Given the raw image file context, the function parses into pixel form
 *
 * @param image_file
 * @return ImageData
 */
auto ImageParser::parse(std::vector<uint8_t> image_file) -> ImageData {
    ImageData result;
    int channels;

    // Parse
    unsigned char* data = stbi_load_from_memory(
        image_file.data(), image_file.size(), &result.width, &result.height,
        &channels, DESIRED_CHANNELS);

    // Return if data is null
    if (!data) {
        return result;
    }

    // Assign pixels data
    int total_pixels = result.width * result.height * 3;
    result.pixels.assign(data, data + total_pixels);
    result.success = true;

    stbi_image_free(data);
    return result;
}

/**
 * @brief Callback function for stbi_write_png_to_func
 *
 * @param context
 * @param data
 * @param size
 */
auto ImageParser::stbiWriteCallback(void* context, void* data, int size)
    -> void {
    auto* vec = static_cast<std::vector<uint8_t>*>(context);
    auto* byte_data = static_cast<uint8_t*>(data);
    vec->insert(vec->end(), byte_data, byte_data + size);
}

/**
 * @brief Given pixels data(handle by function `parse`), restore to .png byte
 * stream
 *
 * @param width
 * @param height
 * @param file_data
 * @return std::vector<uint8_t>
 */
auto ImageParser::restore(int width, int height, std::vector<uint8_t> file_data)
    -> std::vector<uint8_t> {
    std::vector<uint8_t> result;
    int sucess = stbi_write_png_to_func(
        stbiWriteCallback, &result, width, height, DESIRED_CHANNELS,
        file_data.data(), width * DESIRED_CHANNELS);

    if (!sucess) {
        return {};
    }
    return result;
}

}  // namespace core

}  // namespace compressor
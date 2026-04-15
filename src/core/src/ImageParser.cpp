
#include "ImageParser.hpp"

#include <cstdint>
#include <vector>

#include "stb_image.h"

namespace compressor {
namespace core {
ImageData ImageParser::parse(const std::vector<uint8_t> &image_file) {
    ImageData result;
    int channels;

    // Parse
    unsigned char *data = stbi_load_from_memory(
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

}  // namespace core

}  // namespace compressor
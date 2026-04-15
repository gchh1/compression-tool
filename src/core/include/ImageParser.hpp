#pragma once

#include <cstdint>
#include <vector>

namespace compressor {
namespace core {
struct ImageData {
    int width;
    int height;
    std::vector<uint8_t> pixels;
    bool success = false;
};

class ImageParser {
   public:
    /** @brief Parse the original image byte flow to my ImageData */
    static ImageData parse(const std::vector<uint8_t>& image_file);

   private:
    static constexpr int DESIRED_CHANNELS = 3;
};
}  // namespace core
}  // namespace compressor
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
    static auto parse(std::vector<uint8_t> image_file) -> ImageData;

    static auto restore(int width, int height, std::vector<uint8_t> file_data)
        -> std::vector<uint8_t>;

   private:
    static auto stbiWriteCallback(void* context, void* data, int size) -> void;

    static constexpr int DESIRED_CHANNELS = 3;
};
}  // namespace core
}  // namespace compressor
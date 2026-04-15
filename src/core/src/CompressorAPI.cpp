
#include "CompressorAPI.hpp"

#include <vector>

#include "Archiver.hpp"
#include "compressor.hpp"


namespace compressor {
namespace core {
namespace api {

CompressorResult CompressorAPI::compress(const std::vector<InputFile>& files,
                                         int image_quality) {
    std::vector<WebFile> cpp_files;

    for (const auto& file : files) {
        core::WebFile web_file;
        web_file.name = file.filename;

        if (file.is_image) {
            // 🖼️ 图片通道：解析 -> 量化差分 -> 加上宽高头
            core::ImageData img_data = core::ImageParser::parse(file.content);
            if (img_data.success) {
                std::vector<uint8_t> processed_content;

                // 写入 4 字节 Width 和 4 字节 Height (极其重要，解压要用)
                processed_content.push_back((img_data.width >> 24) & 0xFF);
                processed_content.push_back((img_data.width >> 16) & 0xFF);
                processed_content.push_back((img_data.width >> 8) & 0xFF);
                processed_content.push_back(img_data.width & 0xFF);

                processed_content.push_back((img_data.height >> 24) & 0xFF);
                processed_content.push_back((img_data.height >> 16) & 0xFF);
                processed_content.push_back((img_data.height >> 8) & 0xFF);
                processed_content.push_back(img_data.height & 0xFF);

                // 进行 Delta 压缩
                auto encoded =
                    algorithm::Delta::encode(img_data.pixels, image_quality);
                processed_content.insert(processed_content.end(),
                                         encoded.begin(), encoded.end());

                web_file.content = processed_content;
            }
        } else {
            web_file.content = file.content;
        }
        cpp_files.push_back(web_file);
    }

    auto packed_data = core::Archiver::pack(cpp_files);
    core::DeflateCompressor engine;
    return engine.compress(packed_data);
}
}  // namespace api

}  // namespace core

}  // namespace compressor
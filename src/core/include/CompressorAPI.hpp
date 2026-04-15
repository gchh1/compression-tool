#pragma once
#include <cstdint>
#include <string>
#include <vector>

// 引入你所有写好的完美零件
#include "Archiver.hpp"
#include "DeflateCompressor.hpp"
#include "Delta.hpp"
#include "compressor.hpp"

namespace compressor {
namespace core {
namespace api {

// Input structure pass by front end
struct InputFile {
    std::string filename;
    std::vector<uint8_t> content;
    bool is_image;
};

// Output structure handle by compressor
struct OutputFile {
    std::string filename;
    std::vector<uint8_t> content;
    bool is_image;
    int width = 0;
    int height = 0;
};

class CompressorAPI {
   public:
    static CompressorResult compress(const std::vector<InputFile>& files,
                                     int image_quality);

    static CompressorResult decompress(
        const std::vector<uint8_t>& compressed_data) {
        std::vector<OutputFile> result_files;

        // 1. Deflate 解压
        core::DeflateCompressor engine;
        auto decompressed_result = engine.decompress(compressed_data);

        // 2. Archiver 拆包
        auto unpacked_files = core::Archiver::unpack(decompressed_result.data);

        // 3. 智能路由还原
        for (const auto& file : unpacked_files) {
            OutputFile out_file;
            out_file.filename = file.name;

            // 简单以后缀名判断是否需要走图片逆向通道
            std::string ext = "";
            size_t dot_pos = file.name.find_last_of('.');
            if (dot_pos != std::string::npos)
                ext = file.name.substr(dot_pos + 1);

            if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "bmp") {
                out_file.is_image = true;

                // 提取前 8 个字节的宽和高
                const auto& data = file.content;
                if (data.size() > 8) {
                    out_file.width = (data[0] << 24) | (data[1] << 16) |
                                     (data[2] << 8) | data[3];
                    out_file.height = (data[4] << 24) | (data[5] << 16) |
                                      (data[6] << 8) | data[7];

                    // 提取后面的差分数据
                    std::vector<uint8_t> delta_data(data.begin() + 8,
                                                    data.end());

                    // 逆向解压！
                    out_file.content = algorithm::Delta::decode(delta_data);
                }
            } else {
                out_file.is_image = false;
                out_file.content = file.content;
            }
            result_files.push_back(out_file);
        }
    }
};

}  // namespace api
}  // namespace core
}  // namespace compressor
#pragma once
#include <cstdint>
#include <string>
#include <vector>

// 引入你所有写好的完美零件
#include "Archiver.hpp"
#include "DeflateCompressor.hpp"
#include "Delta.hpp"
#include "ImageParser.hpp"

namespace compressor {
namespace api {

// 前端传给 C++ 的输入结构
struct InputFile {
    std::string filename;
    std::vector<uint8_t> content;  // 原始字节流
    bool is_image;                 // 是否为图片
};

// C++ 传给前端的解压输出结构
struct OutputFile {
    std::string filename;
    std::vector<uint8_t> content;  // 还原后的字节流
    bool is_image;                 // 是否为图片
    int width = 0;                 // 如果是图片，附带宽度
    int height = 0;                // 如果是图片，附带高度
};

class WebCompressorAPI {
   public:
    // ==========================================
    // 🚀 1. 终极打包与压缩接口
    // ==========================================
    static core::CompressorResult compressProject(
        const std::vector<InputFile>& files, int image_quality) {
        std::vector<core::WebFile> cpp_files;

        for (const auto& file : files) {
            core::WebFile web_file;
            web_file.name = file.filename;

            if (file.is_image) {
                // 🖼️ 图片通道：解析 -> 量化差分 -> 加上宽高头
                core::ImageData img_data =
                    core::ImageParser::parse(file.content);
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
                    auto encoded = algorithm::Delta::encode(img_data.pixels,
                                                            image_quality);
                    processed_content.insert(processed_content.end(),
                                             encoded.begin(), encoded.end());

                    web_file.content = processed_content;
                }
            } else {
                // 📄 文本通道：原封不动
                web_file.content = file.content;
            }
            cpp_files.push_back(web_file);
        }

        // 调用打包器 -> 终极压缩器
        auto packed_data = core::Archiver::pack(cpp_files);
        core::DeflateCompressor engine;
        return engine.compress(packed_data);
    }

    // ==========================================
    // 📦 2. 终极解压与拆包接口
    // ==========================================
    static std::vector<OutputFile> decompressProject(
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

        return result_files;
    }
};

}  // namespace api
}  // namespace compressor
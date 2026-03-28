// Include lib here
#include "dummy_compressor.hpp"

#include <cstdint>
#include <vector>

#include "compressor.hpp"

namespace compressor {
namespace core {

auto DummyCompressor::compress(const std::vector<uint8_t>& input)
    -> CompressResult {
    auto start = std::chrono::high_resolution_clock::now();

    CompressResult result;
    result.original_size = input.size();

    // 假装我们在压缩，其实只是把数据原封不动拷过去
    result.data = input;
    result.compressed_size = result.data.size();

    // 计算压缩率: (原大小 - 压缩后大小) / 原大小 * 100%
    if (result.original_size > 0) {
        result.compression_ratio =
            static_cast<double>(result.original_size - result.compressed_size) /
            result.original_size * 100.0;
    } else {
        result.compression_ratio = 0.0;
    }

    // 记录结束时间并计算毫秒
    auto end = std::chrono::high_resolution_clock::now();
    result.time_ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    return result;
}

auto DummyCompressor::decompress(const std::vector<uint8_t>& input)
    -> CompressResult {
    auto start = std::chrono::high_resolution_clock::now();

    CompressResult result;
    result.original_size =
        input.size();  // 这里的 original 指的是解压前的输入大小
    result.data = input;
    result.compressed_size = result.data.size();

    auto end = std::chrono::high_resolution_clock::now();
    result.time_ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    return result;
}

auto DummyCompressor::get_algorithm_name(void) -> std::string {
    return std::string("Dummy Compressor");
}
}  // namespace core

}  // namespace compressor

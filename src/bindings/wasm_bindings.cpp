#include <emscripten/bind.h>

#include "DeflateCompressor.hpp"

using namespace emscripten;
using namespace compressor::core;

// 注册绑定
EMSCRIPTEN_BINDINGS(compression_module) {
    // 1. 告诉 JS 怎么理解 std::vector<uint8_t>
    register_vector<uint8_t>("VectorUInt8");

    // 2. 暴露你的 CompressorResult 结构体
    value_object<CompressorResult>("CompressorResult")
        .field("data", &CompressorResult::data)
        .field("original_size", &CompressorResult::original_size)
        .field("compressed_size", &CompressorResult::compressed_size)
        .field("compression_ratio", &CompressorResult::compression_ratio)
        .field("time_ms", &CompressorResult::time_ms);

    // 3. 暴露你的核心引擎类
    class_<DeflateCompressor>("DeflateCompressor")
        .constructor<>()
        .function("compress", &DeflateCompressor::compress)
        .function("decompress", &DeflateCompressor::decompress)
        .function("get_algorithm_name", &DeflateCompressor::get_algorithm_name);
}
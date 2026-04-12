#include <emscripten/bind.h>

#include "Archiver.hpp"
#include "DeflateCompressor.hpp"

using namespace emscripten;
using namespace compressor::core;

// 注册绑定
EMSCRIPTEN_BINDINGS(compression_module) {
    // 1. 告诉 JS 怎么理解 std::vector<uint8_t>
    register_vector<uint8_t>("VectorUInt8");

    // Expose struct
    value_object<CompressorResult>("CompressorResult")
        .field("data", &CompressorResult::data)
        .field("original_size", &CompressorResult::original_size)
        .field("compressed_size", &CompressorResult::compressed_size)
        .field("compression_ratio", &CompressorResult::compression_ratio)
        .field("time_ms", &CompressorResult::time_ms);

    value_object<WebFile>("WebFile")
        .field("name", &WebFile::name)
        .field("content", &WebFile::content);

    register_vector<WebFile>("vectorWebFile");

    // Expose class
    class_<DeflateCompressor>("DeflateCompressor")
        .constructor<>()
        .function("compress", &DeflateCompressor::compress)
        .function("decompress", &DeflateCompressor::decompress)
        .function("get_algorithm_name", &DeflateCompressor::get_algorithm_name);

    class_<Archiver>("Archiver")
        .class_function("pack", &Archiver::pack)
        .class_function("unpack", &Archiver::unpack);
}
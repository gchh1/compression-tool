#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstdio>
#include <string>
#include <vector>

#include "api.hpp"

using namespace emscripten;

namespace {

using AlgorithmID = compressor::api::AlgorithmID;

auto jsToAlgorithmChain(val jsChain) -> std::vector<AlgorithmID> {
    std::vector<AlgorithmID> chain;
    int len = jsChain["length"].as<int>();
    for (int i = 0; i < len; ++i)
        chain.push_back(jsChain[i].as<AlgorithmID>());
    return chain;
}

auto jsToVector(val jsData) -> std::vector<uint8_t> {
    std::vector<uint8_t> data;
    int len = jsData["length"].as<int>();
    data.reserve(len);
    for (int i = 0; i < len; ++i)
        data.push_back(jsData[i].as<uint8_t>());
    return data;
}

// ---- compress / decompress wrappers ----

auto wasmCompress(val jsData, val jsChain) -> val {
    auto chain = jsToAlgorithmChain(jsChain);
    auto result = compressor::api::compress(jsToVector(jsData), chain);

    auto obj = val::object();
    obj.set("data", result.data);
    obj.set("originalSize", result.original_size);
    obj.set("compressedSize", result.compressed_size);
    obj.set("compressionRatio", result.compression_ratio);
    obj.set("timeMs", result.time_ms);
    obj.set("success", result.success);
    obj.set("errorMessage", result.error_message);
    return obj;
}

auto wasmDecompress(val jsData, val jsChain) -> val {
    auto chain = jsToAlgorithmChain(jsChain);
    auto result = compressor::api::decompress(jsToVector(jsData), chain);

    auto obj = val::object();
    obj.set("data", result.data);
    obj.set("originalSize", result.original_size);
    obj.set("compressedSize", result.compressed_size);
    obj.set("compressionRatio", result.compression_ratio);
    obj.set("timeMs", result.time_ms);
    obj.set("success", result.success);
    obj.set("errorMessage", result.error_message);
    return obj;
}

// ---- pack / unpack wrappers ----

auto wasmPackAndCompress(val jsFiles, val jsChain) -> std::vector<uint8_t> {
    auto console = val::global("console");
    std::vector<compressor::api::WebFile> files;
    int len = jsFiles["length"].as<int>();
    console.call<void>("log", std::string("packAndCompress: ") + std::to_string(len) + " input files");
    for (int i = 0; i < len; ++i) {
        val f = jsFiles[i];
        compressor::api::WebFile wf;
        wf.name = f["name"].as<std::string>();
        wf.content = jsToVector(f["content"]);
        console.call<void>("log", std::string("  file[") + std::to_string(i) + "]: " + wf.name + " size=" + std::to_string(wf.content.size()));
        files.push_back(std::move(wf));
    }
    auto chain = jsToAlgorithmChain(jsChain);
    auto result = compressor::api::packAndCompress(files, chain);
    console.call<void>("log", std::string("packAndCompress result size: ") + std::to_string(result.size()));
    return result;
}

// Static storage for unpacked files to avoid dangling references
static std::vector<compressor::api::WebFile> s_lastUnpacked;

auto wasmDecompressAndUnpack(val jsData) -> int {
    auto console = val::global("console");
    auto data = jsToVector(jsData);
    console.call<void>("log", std::string("decompressAndUnpack: input size=") + std::to_string(data.size()));
    s_lastUnpacked = compressor::api::decompressAndUnpack(data);
    console.call<void>("log", std::string("decompressAndUnpack: got ") + std::to_string(s_lastUnpacked.size()) + " entries");
    return static_cast<int>(s_lastUnpacked.size());
}

auto wasmGetUnpackedCount() -> int {
    return static_cast<int>(s_lastUnpacked.size());
}

auto wasmGetUnpackedName(int index) -> std::string {
    if (index < 0 || index >= static_cast<int>(s_lastUnpacked.size()))
        return {};
    return s_lastUnpacked[index].name;
}

auto wasmGetUnpackedContent(int index) -> std::vector<uint8_t> {
    if (index < 0 || index >= static_cast<int>(s_lastUnpacked.size()))
        return {};
    return s_lastUnpacked[index].content;
}

}  // namespace

EMSCRIPTEN_BINDINGS(compression_module) {
    enum_<AlgorithmID>("AlgorithmID")
        .value("None", AlgorithmID::None)
        .value("Deflate", AlgorithmID::Deflate)
        .value("Inflate", AlgorithmID::Inflate)
        .value("DeltaEncode", AlgorithmID::DeltaEncode)
        .value("DeltaDecode", AlgorithmID::DeltaDecode);

    register_vector<uint8_t>("VectorUInt8");

    function("compress", &wasmCompress);
    function("decompress", &wasmDecompress);
    function("packAndCompress", &wasmPackAndCompress);
    function("decompressAndUnpack", &wasmDecompressAndUnpack);
    function("getUnpackedCount", &wasmGetUnpackedCount);
    function("getUnpackedName", &wasmGetUnpackedName);
    function("getUnpackedContent", &wasmGetUnpackedContent);
}

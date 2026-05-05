#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <span>
#include <vector>

#include "api.hpp"

namespace py = pybind11;

using namespace compressor::api;

namespace {

auto pyCompress(const std::vector<uint8_t>& data,
                const std::vector<AlgorithmID>& chain) -> CompressResult {
    return compress(data, std::span<const AlgorithmID>(chain));
}

auto pyDecompress(const std::vector<uint8_t>& data,
                  const std::vector<AlgorithmID>& chain) -> CompressResult {
    return decompress(data, std::span<const AlgorithmID>(chain));
}

auto pyPackAndCompress(const std::vector<WebFile>& files,
                       const std::vector<AlgorithmID>& chain)
    -> std::vector<uint8_t> {
    return packAndCompress(files, std::span<const AlgorithmID>(chain));
}

#ifndef __EMSCRIPTEN__

auto pyCompressFile(const std::string& input_path,
                    const std::string& output_path,
                    const std::vector<AlgorithmID>& chain) -> CompressResult {
    return compressFile(input_path, output_path,
                        std::span<const AlgorithmID>(chain));
}

auto pyDecompressFile(const std::string& input_path,
                      const std::string& output_path,
                      const std::vector<AlgorithmID>& chain) -> CompressResult {
    return decompressFile(input_path, output_path,
                          std::span<const AlgorithmID>(chain));
}

auto pyCompressDirectory(const std::string& dir_path,
                         const std::string& output_path,
                         const std::vector<AlgorithmID>& chain) -> CompressResult {
    return compressDirectory(dir_path, output_path,
                             std::span<const AlgorithmID>(chain));
}

#endif

}  // namespace

PYBIND11_MODULE(core_engine, m) {
    m.doc() = "Web Compressor C++ Core Engine";

    py::enum_<AlgorithmID>(m, "AlgorithmID")
        .value("None", AlgorithmID::None)
        .value("Deflate", AlgorithmID::Deflate)
        .value("Inflate", AlgorithmID::Inflate)
        .value("DeltaEncode", AlgorithmID::DeltaEncode)
        .value("DeltaDecode", AlgorithmID::DeltaDecode);

    py::class_<BlockInfo>(m, "BlockInfo")
        .def(py::init<>())
        .def_readwrite("block_index", &BlockInfo::block_index)
        .def_readwrite("literal_count", &BlockInfo::literal_count)
        .def_readwrite("match_count", &BlockInfo::match_count)
        .def_readwrite("ll_tree_bits", &BlockInfo::ll_tree_bits)
        .def_readwrite("dist_tree_bits", &BlockInfo::dist_tree_bits)
        .def_readwrite("output_bytes", &BlockInfo::output_bytes)
        .def_readwrite("ll_code_lengths", &BlockInfo::ll_code_lengths)
        .def_readwrite("dist_code_lengths", &BlockInfo::dist_code_lengths);

    py::class_<BlockProfile>(m, "BlockProfile")
        .def(py::init<>())
        .def_readwrite("blocks", &BlockProfile::blocks);

    py::class_<CompressResult>(m, "CompressResult")
        .def(py::init<>())
        .def_readwrite("data", &CompressResult::data)
        .def_readwrite("original_size", &CompressResult::original_size)
        .def_readwrite("compressed_size", &CompressResult::compressed_size)
        .def_readwrite("compression_ratio", &CompressResult::compression_ratio)
        .def_readwrite("time_ms", &CompressResult::time_ms)
        .def_readwrite("success", &CompressResult::success)
        .def_readwrite("error_message", &CompressResult::error_message)
        .def_readwrite("block_profile", &CompressResult::block_profile);

    py::class_<WebFile>(m, "WebFile")
        .def(py::init<>())
        .def_readwrite("name", &WebFile::name)
        .def_readwrite("content", &WebFile::content);

    m.def("compress", &pyCompress,
          "Compress a single buffer with the given algorithm chain");
    m.def("decompress", &pyDecompress,
          "Decompress a single buffer with the given algorithm chain");
    m.def("pack_and_compress", &pyPackAndCompress,
          "Pack multiple files into a compressed archive");
    m.def("decompress_and_unpack", &decompressAndUnpack,
          "Unpack a compressed archive back into individual files");

#ifndef __EMSCRIPTEN__
    m.def("compress_file", &pyCompressFile,
          "Stream-compress a single file to output_path");
    m.def("decompress_file", &pyDecompressFile,
          "Stream-decompress a single file to output_path");
    m.def("compress_directory", &pyCompressDirectory,
          "Recursively pack and compress a directory into an archive file");
    m.def("decompress_and_unpack_to_disk", &decompressAndUnpackToDisk,
          "Unpack a compressed archive to disk, preserving directory structure");
#endif
}

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "ICompressor.hpp"
#include "DeflateCompressor.hpp"
#include "LZSSCompressor.hpp"
#include "Archiver.hpp"

namespace py = pybind11;
using namespace compressor::core;

PYBIND11_MODULE(core_engine, m) {
    m.doc() = "Web Compressor C++ Core Engine";

    // ===== 数据结构 =====

    py::class_<CompressorResult>(m, "CompressorResult")
        .def(py::init<>())
        .def_readwrite("data", &CompressorResult::data)
        .def_readwrite("original_size", &CompressorResult::original_size)
        .def_readwrite("compressed_size", &CompressorResult::compressed_size)
        .def_readwrite("compression_ratio", &CompressorResult::compression_ratio)
        .def_readwrite("time_ms", &CompressorResult::time_ms)
        .def_readwrite("success", &CompressorResult::success)
        .def_readwrite("error_message", &CompressorResult::error_message);

    py::class_<WebFile>(m, "WebFile")
        .def(py::init<>())
        .def_readwrite("name", &WebFile::name)
        .def_readwrite("content", &WebFile::content);

    // ===== 算法枚举 =====

    py::enum_<CompressorAlgorithm>(m, "CompressorAlgorithm")
        .value("DEFLATE", CompressorAlgorithm::Deflate)
        .value("LZSS", CompressorAlgorithm::LZSS)
        .export_values();

    // ===== 压缩器接口 =====

    py::class_<ICompressor, std::shared_ptr<ICompressor>>(m, "ICompressor")
        .def("compress", &ICompressor::compress)
        .def("decompress", &ICompressor::decompress)
        .def("get_algorithm_name", &ICompressor::get_algorithm_name)
        .def("compress_batch", &ICompressor::compress_batch);

    py::class_<DeflateCompressor, ICompressor,
               std::shared_ptr<DeflateCompressor>>(m, "DeflateCompressor")
        .def(py::init<>());

    py::class_<LZSSCompressor, ICompressor,
               std::shared_ptr<LZSSCompressor>>(m, "LZSSCompressor")
        .def(py::init<>());

    // ===== 工厂 =====

    py::class_<CompressorFactory>(m, "CompressorFactory")
        .def_static("create", &CompressorFactory::create)
        .def_static("list_algorithms", &CompressorFactory::list_algorithms);

    // ===== 打包器 =====

    py::class_<Archiver>(m, "Archiver")
        .def_static("pack", &Archiver::pack)
        .def_static("unpack", &Archiver::unpack)
        .def_static("pack_and_compress", &Archiver::pack_and_compress)
        .def_static("decompress_and_unpack", &Archiver::decompress_and_unpack);
}

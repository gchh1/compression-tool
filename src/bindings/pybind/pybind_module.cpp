#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "ICompressor.hpp"
#include "DeflateCompressor.hpp"
#include "LZSSCompressor.hpp"
#include "LZMineCompressor.hpp"
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

    // ===== 算法枚举 =====

    py::enum_<CompressorAlgorithm>(m, "CompressorAlgorithm")
        .value("DEFLATE", CompressorAlgorithm::Deflate)
        .value("LZSS", CompressorAlgorithm::LZSS)
        .value("LZMINE", CompressorAlgorithm::LZMINE)
        .export_values();

    // ===== 压缩器接口 =====

    py::class_<ICompressor, std::shared_ptr<ICompressor>>(m, "ICompressor")
        .def("compress", &ICompressor::compress)
        .def("decompress", &ICompressor::decompress)
        .def("get_algorithm_name", &ICompressor::get_algorithm_name);

    py::class_<DeflateCompressor, ICompressor,
               std::shared_ptr<DeflateCompressor>>(m, "DeflateCompressor")
        .def(py::init<>());

    py::class_<LZSSCompressor, ICompressor,
               std::shared_ptr<LZSSCompressor>>(m, "LZSSCompressor")
        .def(py::init<>());

    py::class_<LZMineCompressor, ICompressor,
               std::shared_ptr<LZMineCompressor>>(m, "LZMineCompressor")
        .def(py::init<>());

    // ===== 打包器 File 结构体 =====

    py::class_<File>(m, "File")
        .def(py::init<>())
        .def_readwrite("filepath", &File::filepath)
        .def_readwrite("context", &File::context);

    // ===== 打包器 =====

    py::class_<Archiver>(m, "Archiver")
        .def_static("pack", &Archiver::pack)
        .def_static("unpack", &Archiver::unpack);
}

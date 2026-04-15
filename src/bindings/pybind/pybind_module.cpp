#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "Archiver.hpp"
#include "DeflateCompressor.hpp"

namespace py = pybind11;
using namespace compressor::core;

PYBIND11_MODULE(core_engine, m) {
    m.doc() = "Web Compressor C++ Core Engine";

    // 🌟 核心修复：告诉 Python 怎么理解 CompressorResult
    py::class_<CompressorResult>(m, "CompressorResult")
        .def(py::init<>())
        .def_readwrite("data", &CompressorResult::data)
        .def_readwrite("original_size", &CompressorResult::original_size)
        .def_readwrite("compressed_size", &CompressorResult::compressed_size)
        .def_readwrite("compression_ratio",
                       &CompressorResult::compression_ratio)
        .def_readwrite("time_ms", &CompressorResult::time_ms);

    py::class_<WebFile>(m, "WebFile")
        .def(py::init<>())
        .def_readwrite("name", &WebFile::name)
        .def_readwrite("content", &WebFile::content);

    py::class_<Archiver>(m, "Archiver")
        .def_static("pack", &Archiver::pack)
        .def_static("unpack", &Archiver::unpack);

    py::class_<DeflateCompressor>(m, "DeflateCompressor")
        .def(py::init<>())
        .def("compress", &DeflateCompressor::compress)
        .def("decompress", &DeflateCompressor::decompress);
}
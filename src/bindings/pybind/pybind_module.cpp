#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "ICompressor.hpp"
#include "DeflateCompressor.hpp"
#include "LZSSCompressor.hpp"
#include "LZMineCompressor.hpp"
#include "MyFlateCompressor.hpp"
#include "GzipCompressor.hpp"
#include "Archiver.hpp"
#include "api.hpp"
#include "AlgorithmFactory.hpp"

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
        .def("compress", &ICompressor::compress, py::call_guard<py::gil_scoped_release>())
        .def("decompress", &ICompressor::decompress, py::call_guard<py::gil_scoped_release>())
        .def("get_algorithm_name", &ICompressor::get_algorithm_name);

    py::class_<DeflateCompressor, ICompressor,
               std::shared_ptr<DeflateCompressor>>(m, "DeflateCompressor")
        .def(py::init<>())
        .def("set_search_size", &DeflateCompressor::set_search_size)
        .def("get_search_size", &DeflateCompressor::get_search_size)
        .def("set_min_match", &DeflateCompressor::set_min_match)
        .def("get_min_match", &DeflateCompressor::get_min_match)
        .def("set_max_chain_length", &DeflateCompressor::set_max_chain_length)
        .def("get_max_chain_length", &DeflateCompressor::get_max_chain_length);

    py::class_<LZSSCompressor, ICompressor,
               std::shared_ptr<LZSSCompressor>>(m, "LZSSCompressor")
        .def(py::init<>())
        .def("set_search_size", &LZSSCompressor::set_search_size)
        .def("get_search_size", &LZSSCompressor::get_search_size)
        .def("set_min_match", &LZSSCompressor::set_min_match)
        .def("get_min_match", &LZSSCompressor::get_min_match)
        .def("set_lookahead_size", &LZSSCompressor::set_lookahead_size)
        .def("get_lookahead_size", &LZSSCompressor::get_lookahead_size);

    py::class_<LZMineCompressor, ICompressor,
               std::shared_ptr<LZMineCompressor>>(m, "LZMineCompressor")
        .def(py::init<>())
        .def("set_search_size", &LZMineCompressor::set_search_size)
        .def("get_search_size", &LZMineCompressor::get_search_size)
        .def("set_lookahead_size", &LZMineCompressor::set_lookahead_size)
        .def("get_lookahead_size", &LZMineCompressor::get_lookahead_size)
        .def("set_dp_depth", &LZMineCompressor::set_dp_depth)
        .def("get_dp_depth", &LZMineCompressor::get_dp_depth)
        .def("set_dp_range", &LZMineCompressor::set_dp_range)
        .def("get_dp_range", &LZMineCompressor::get_dp_range)
        .def("get_dp_visualization", &LZMineCompressor::get_dp_visualization,
             py::arg("data"), py::arg("range") = 3);

    py::class_<compressor::algorithm::LZMine::Triple>(m, "LZMineTriple")
        .def_readonly("offset", &compressor::algorithm::LZMine::Triple::offset)
        .def_readonly("length", &compressor::algorithm::LZMine::Triple::length)
        .def_readonly("next_byte", &compressor::algorithm::LZMine::Triple::next_byte);

    py::class_<compressor::algorithm::LZMine::DPCandidate>(m, "LZMineDPCandidate")
        .def_readonly("offset", &compressor::algorithm::LZMine::DPCandidate::offset)
        .def_readonly("length", &compressor::algorithm::LZMine::DPCandidate::length)
        .def_readonly("next_byte", &compressor::algorithm::LZMine::DPCandidate::next_byte)
        .def_readonly("is_chosen", &compressor::algorithm::LZMine::DPCandidate::is_chosen);

    py::class_<compressor::algorithm::LZMine::DPState>(m, "LZMineDPState")
        .def_readonly("position", &compressor::algorithm::LZMine::DPState::position)
        .def_readonly("reachable", &compressor::algorithm::LZMine::DPState::reachable)
        .def_readonly("token_count", &compressor::algorithm::LZMine::DPState::token_count)
        .def_readonly("predecessor", &compressor::algorithm::LZMine::DPState::predecessor)
        .def_readonly("choice", &compressor::algorithm::LZMine::DPState::choice);

    py::class_<compressor::algorithm::LZMine::DPStep>(m, "LZMineDPStep")
        .def_readonly("position", &compressor::algorithm::LZMine::DPStep::position)
        .def_readonly("candidates", &compressor::algorithm::LZMine::DPStep::candidates)
        .def_readonly("best_token_count", &compressor::algorithm::LZMine::DPStep::best_token_count);

    py::class_<compressor::algorithm::LZMine::DPVisualization>(m, "LZMineDPVisualization")
        .def_readonly("steps", &compressor::algorithm::LZMine::DPVisualization::steps)
        .def_readonly("dp_array", &compressor::algorithm::LZMine::DPVisualization::dp_array)
        .def_readonly("optimal_path", &compressor::algorithm::LZMine::DPVisualization::optimal_path)
        .def_readonly("input_length", &compressor::algorithm::LZMine::DPVisualization::input_length)
        .def_readonly("search_size", &compressor::algorithm::LZMine::DPVisualization::search_size)
        .def_readonly("lookahead_size", &compressor::algorithm::LZMine::DPVisualization::lookahead_size);

    py::class_<MyFlateCompressor, ICompressor,
               std::shared_ptr<MyFlateCompressor>>(m, "MyFlateCompressor")
        .def(py::init<>())
        .def("set_search_size", &MyFlateCompressor::set_search_size)
        .def("get_search_size", &MyFlateCompressor::get_search_size)
        .def("set_lookahead_size", &MyFlateCompressor::set_lookahead_size)
        .def("get_lookahead_size", &MyFlateCompressor::get_lookahead_size)
        .def("set_min_match", &MyFlateCompressor::set_min_match)
        .def("get_min_match", &MyFlateCompressor::get_min_match)
        .def("set_max_chain_length", &MyFlateCompressor::set_max_chain_length)
        .def("get_max_chain_length", &MyFlateCompressor::get_max_chain_length)
        .def("set_dp_depth", &MyFlateCompressor::set_dp_depth)
        .def("get_dp_depth", &MyFlateCompressor::get_dp_depth)
        .def("set_dp_sub_match_max", &MyFlateCompressor::set_dp_sub_match_max)
        .def("get_dp_sub_match_max", &MyFlateCompressor::get_dp_sub_match_max);

    py::class_<GzipCompressor, ICompressor,
               std::shared_ptr<GzipCompressor>>(m, "GzipCompressor")
        .def(py::init<>())
        .def("set_compression_level", &GzipCompressor::set_compression_level)
        .def("get_compression_level", &GzipCompressor::get_compression_level);

    // ===== 打包器 File 结构体 =====

    py::class_<File>(m, "File")
        .def(py::init<>())
        .def_readwrite("filepath", &File::filepath)
        .def_readwrite("context", &File::context);

    // ===== 打包器 =====

    py::class_<Archiver>(m, "Archiver")
        .def_static("pack", &Archiver::pack)
        .def_static("unpack", &Archiver::unpack);

    // ===== 流式分块 Pipeline API =====

    py::enum_<compressor::core::AlgorithmID>(m, "AlgorithmID")
        .value("NONE", compressor::core::AlgorithmID::None)
        .value("DEFLATE", compressor::core::AlgorithmID::Deflate)
        .value("INFLATE", compressor::core::AlgorithmID::Inflate)
        .value("DELTA_ENCODE", compressor::core::AlgorithmID::DeltaEncode)
        .value("DELTA_DECODE", compressor::core::AlgorithmID::DeltaDecode)
        .value("LZSS", compressor::core::AlgorithmID::LZSS)
        .value("LZSS_DECOMPRESS", compressor::core::AlgorithmID::LZSSDecompress)
        .value("LZMINE", compressor::core::AlgorithmID::LZMine)
        .value("LZMINE_DECOMPRESS", compressor::core::AlgorithmID::LZMineDecompress)
        .value("MYFLATE", compressor::core::AlgorithmID::MyFlate)
        .export_values();

    py::class_<compressor::api::CompressResult>(m, "PipelineCompressResult")
        .def(py::init<>())
        .def_readwrite("data", &compressor::api::CompressResult::data)
        .def_readwrite("original_size", &compressor::api::CompressResult::original_size)
        .def_readwrite("compressed_size", &compressor::api::CompressResult::compressed_size)
        .def_readwrite("compression_ratio", &compressor::api::CompressResult::compression_ratio)
        .def_readwrite("time_ms", &compressor::api::CompressResult::time_ms)
        .def_readwrite("success", &compressor::api::CompressResult::success)
        .def_readwrite("error_message", &compressor::api::CompressResult::error_message);

    m.def("pipeline_compress",
          [](const std::vector<uint8_t>& data,
             const std::vector<compressor::core::AlgorithmID>& chain)
              -> compressor::api::CompressResult {
              return compressor::api::compress(data, chain);
          },
          py::arg("data"), py::arg("chain"),
          py::call_guard<py::gil_scoped_release>(),
          "Compress data using a pipeline of algorithms with streaming chunking");

    m.def("pipeline_decompress",
          [](const std::vector<uint8_t>& data,
             const std::vector<compressor::core::AlgorithmID>& chain)
              -> compressor::api::CompressResult {
              return compressor::api::decompress(data, chain);
          },
          py::arg("data"), py::arg("chain"),
          py::call_guard<py::gil_scoped_release>(),
          "Decompress data using a pipeline of algorithms");

    m.def("pipeline_compress_file",
          [](const std::string& input_path,
             const std::string& output_path,
             const std::vector<compressor::core::AlgorithmID>& chain)
              -> compressor::api::CompressResult {
              return compressor::api::compressFile(input_path, output_path, chain);
          },
          py::arg("input_path"), py::arg("output_path"), py::arg("chain"),
          py::call_guard<py::gil_scoped_release>(),
          "Streaming compress a file in chunks");

    m.def("pipeline_decompress_file",
          [](const std::string& input_path,
             const std::string& output_path,
             const std::vector<compressor::core::AlgorithmID>& chain)
              -> compressor::api::CompressResult {
              return compressor::api::decompressFile(input_path, output_path, chain);
          },
          py::arg("input_path"), py::arg("output_path"), py::arg("chain"),
          py::call_guard<py::gil_scoped_release>(),
          "Streaming decompress a file in chunks");
}

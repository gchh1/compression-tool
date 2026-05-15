#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include "api.hpp"
#include "AlgorithmFactory.hpp"
#include "DiskVizObserver.hpp"  // compressor::viz
#include "LZDP.hpp"
#include "VizEvent.hpp"  // compressor::viz types

namespace py = pybind11;
using namespace compressor::core;

void init_ade(py::module_& m);
void init_ea(py::module_& m);

PYBIND11_MODULE(core_engine, m) {
    m.doc() = "Web Compressor C++ Core Engine";

    // ===== LZDP 数据结构（DP 可视化）=====

    py::class_<compressor::algorithm::LZDP::Triple>(m, "LZDPTriple")
        .def_readonly("offset", &compressor::algorithm::LZDP::Triple::offset)
        .def_readonly("length", &compressor::algorithm::LZDP::Triple::length)
        .def_readonly("literal", &compressor::algorithm::LZDP::Triple::literal);

    py::class_<compressor::algorithm::LZDP::DPCandidate>(m, "LZDPDPCandidate")
        .def_readonly("offset", &compressor::algorithm::LZDP::DPCandidate::offset)
        .def_readonly("length", &compressor::algorithm::LZDP::DPCandidate::length)
        .def_readonly("literal", &compressor::algorithm::LZDP::DPCandidate::literal)
        .def_readonly("is_chosen", &compressor::algorithm::LZDP::DPCandidate::is_chosen);

    py::class_<compressor::algorithm::LZDP::DPState>(m, "LZDPDPState")
        .def_readonly("position", &compressor::algorithm::LZDP::DPState::position)
        .def_readonly("reachable", &compressor::algorithm::LZDP::DPState::reachable)
        .def_readonly("token_count", &compressor::algorithm::LZDP::DPState::token_count)
        .def_readonly("predecessor", &compressor::algorithm::LZDP::DPState::predecessor)
        .def_readonly("choice", &compressor::algorithm::LZDP::DPState::choice);

    py::class_<compressor::algorithm::LZDP::DPStep>(m, "LZDPDPStep")
        .def_readonly("position", &compressor::algorithm::LZDP::DPStep::position)
        .def_readonly("candidates", &compressor::algorithm::LZDP::DPStep::candidates)
        .def_readonly("best_token_count", &compressor::algorithm::LZDP::DPStep::best_token_count);

    py::class_<compressor::algorithm::LZDP::DPVisualization>(m, "LZDPDPVisualization")
        .def_readonly("steps", &compressor::algorithm::LZDP::DPVisualization::steps)
        .def_readonly("dp_array", &compressor::algorithm::LZDP::DPVisualization::dp_array)
        .def_readonly("optimal_path", &compressor::algorithm::LZDP::DPVisualization::optimal_path)
        .def_readonly("input_length", &compressor::algorithm::LZDP::DPVisualization::input_length)
        .def_readonly("search_size", &compressor::algorithm::LZDP::DPVisualization::search_size)
        .def_readonly("lookahead_size", &compressor::algorithm::LZDP::DPVisualization::lookahead_size);

    // ===== LZDP 算法类（仅可视化）=====

    py::class_<compressor::algorithm::LZDP>(m, "LZDPViz")
        .def(py::init<size_t, size_t>(),
             py::arg("offset_bits") = 0,
             py::arg("length_bits") = 0)
        .def("autoBitWidth", &compressor::algorithm::LZDP::autoBitWidth)
        .def("set_min_match", &compressor::algorithm::LZDP::set_min_match)
        .def("set_use_flag_encoding", &compressor::algorithm::LZDP::set_use_flag_encoding)
        .def("set_match_engine", &compressor::algorithm::LZDP::set_match_engine)
        .def("get_dp_visualization", &compressor::algorithm::LZDP::get_dp_visualization,
             py::arg("data"), py::arg("search_size"),
             py::arg("lookahead_size"), py::arg("range") = 3,
             py::call_guard<py::gil_scoped_release>());

    // ===== 流式分块 Pipeline API =====

    py::enum_<compressor::core::AlgorithmID>(m, "AlgorithmID")
        .value("NONE", compressor::core::AlgorithmID::None)
        .value("DEFLATE", compressor::core::AlgorithmID::Deflate)
        .value("INFLATE", compressor::core::AlgorithmID::Inflate)
        .value("DELTA_ENCODE", compressor::core::AlgorithmID::DeltaEncode)
        .value("DELTA_DECODE", compressor::core::AlgorithmID::DeltaDecode)
        .value("LZSS", compressor::core::AlgorithmID::LZSS)
        .value("LZSS_DECOMPRESS", compressor::core::AlgorithmID::LZSSDecompress)
        .value("LZDP", compressor::core::AlgorithmID::LZDP)
        .value("LZDP_DECOMPRESS", compressor::core::AlgorithmID::LZDPDecompress)
        .value("LZMINE", compressor::core::AlgorithmID::LZDP) // Deprecated alias
        .value("LZMINE_DECOMPRESS", compressor::core::AlgorithmID::LZDPDecompress) // Deprecated alias
        .value("DPFLATE", compressor::core::AlgorithmID::DPFlate)
        .value("BROTLI", compressor::core::AlgorithmID::Brotli)
        .value("BROTLI_DECOMPRESS", compressor::core::AlgorithmID::BrotliDecompress)
        .value("ZSTD", compressor::core::AlgorithmID::Zstd)
        .value("ZSTD_DECOMPRESS", compressor::core::AlgorithmID::ZstdDecompress)
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

    py::class_<compressor::api::WCXUnpackResult>(m, "WCXUnpackResult")
        .def(py::init<>())
        .def_readwrite("success", &compressor::api::WCXUnpackResult::success)
        .def_readwrite("algo_code", &compressor::api::WCXUnpackResult::algo_code)
        .def_readwrite("original_size", &compressor::api::WCXUnpackResult::original_size)
        .def_readwrite("compressed_size", &compressor::api::WCXUnpackResult::compressed_size)
        .def_readwrite("is_folder", &compressor::api::WCXUnpackResult::is_folder)
        .def_readwrite("original_filename", &compressor::api::WCXUnpackResult::original_filename)
        .def_readwrite("payload", &compressor::api::WCXUnpackResult::payload)
        .def_readwrite("error_message", &compressor::api::WCXUnpackResult::error_message);

    m.def("pack_wcx",
          [](const std::vector<uint8_t>& compressed_data,
             compressor::core::AlgorithmID algorithm,
             size_t original_size,
             const std::string& original_filename,
             bool is_folder) -> std::vector<uint8_t> {
              return compressor::api::pack_wcx(compressed_data, algorithm,
                                              original_size,
                                              original_filename,
                                              is_folder);
          },
          py::arg("compressed_data"),
          py::arg("algorithm"),
          py::arg("original_size"),
          py::arg("original_filename") = "",
          py::arg("is_folder") = false,
          py::call_guard<py::gil_scoped_release>(),
          "Pack WCX header + payload");

    m.def("unpack_wcx",
          [](const std::vector<uint8_t>& data)
              -> compressor::api::WCXUnpackResult {
              return compressor::api::unpack_wcx(data);
          },
          py::arg("data"),
          py::call_guard<py::gil_scoped_release>(),
          "Unpack WCX header and payload");

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
             const std::vector<compressor::core::AlgorithmID>& chain,
             size_t stream_chunk_bytes)
              -> compressor::api::CompressResult {
              return compressor::api::compressFile(input_path, output_path, chain,
                                                    stream_chunk_bytes);
          },
          py::arg("input_path"), py::arg("output_path"), py::arg("chain"),
          py::arg("stream_chunk_bytes") = size_t{0},
          py::call_guard<py::gil_scoped_release>(),
          "Streaming compress a file in chunks (optional stream_chunk_bytes; 0 = default 1 MiB)");

    m.def("pipeline_decompress_file",
          [](const std::string& input_path,
             const std::string& output_path,
             const std::vector<compressor::core::AlgorithmID>& chain,
             size_t stream_chunk_bytes)
              -> compressor::api::CompressResult {
              return compressor::api::decompressFile(input_path, output_path, chain,
                                                      stream_chunk_bytes);
          },
          py::arg("input_path"), py::arg("output_path"), py::arg("chain"),
          py::arg("stream_chunk_bytes") = size_t{0},
          py::call_guard<py::gil_scoped_release>(),
          "Streaming decompress a file in chunks (optional stream_chunk_bytes; 0 = default 1 MiB)");

    // ===== ADE (Algorithm Decision Engine) =====
    init_ade(m);

    // ===== EA (Evolutionary Algorithms) - Parameter Optimizer =====
    init_ea(m);

    // ===== Visualization event types =====

    py::class_<compressor::viz::MatchEvent>(m, "MatchEvent")
        .def(py::init<>())
        .def_readwrite("input_pos", &compressor::viz::MatchEvent::input_pos)
        .def_readwrite("offset", &compressor::viz::MatchEvent::offset)
        .def_readwrite("length", &compressor::viz::MatchEvent::length)
        .def_readwrite("literal", &compressor::viz::MatchEvent::literal);

    py::class_<compressor::viz::BlockBoundary>(m, "BlockBoundary")
        .def(py::init<>())
        .def_readwrite("block_index", &compressor::viz::BlockBoundary::block_index)
        .def_readwrite("input_start", &compressor::viz::BlockBoundary::input_start)
        .def_readwrite("input_bytes", &compressor::viz::BlockBoundary::input_bytes)
        .def_readwrite("literal_count", &compressor::viz::BlockBoundary::literal_count)
        .def_readwrite("match_count", &compressor::viz::BlockBoundary::match_count)
        .def_readwrite("output_bytes", &compressor::viz::BlockBoundary::output_bytes);

    py::class_<compressor::viz::HuffmanTreeBuilt>(m, "HuffmanTreeBuilt")
        .def(py::init<>())
        .def_readwrite("block_index", &compressor::viz::HuffmanTreeBuilt::block_index)
        .def_readwrite("tree_type", &compressor::viz::HuffmanTreeBuilt::tree_type)
        .def_readwrite("alphabet_size", &compressor::viz::HuffmanTreeBuilt::alphabet_size)
        .def_property_readonly("code_lengths",
            [](const compressor::viz::HuffmanTreeBuilt& e) {
                return py::bytes(reinterpret_cast<const char*>(e.code_lengths), 286);
            });

    py::class_<compressor::viz::DPStateEvent>(m, "DPStateEvent")
        .def(py::init<>())
        .def_readwrite("position", &compressor::viz::DPStateEvent::position)
        .def_readwrite("token_count", &compressor::viz::DPStateEvent::token_count)
        .def_readwrite("predecessor", &compressor::viz::DPStateEvent::predecessor)
        .def_readwrite("match_offset", &compressor::viz::DPStateEvent::match_offset)
        .def_readwrite("match_length", &compressor::viz::DPStateEvent::match_length)
        .def_readwrite("is_chosen", &compressor::viz::DPStateEvent::is_chosen);

    // ===== DiskVizObserver =====

    py::class_<compressor::viz::DiskVizObserver>(m, "DiskVizObserver")
        .def(py::init<const std::string&>(),
             py::arg("path"));

    // ===== Viz-aware compression =====

    m.def("pipeline_compress_file_viz",
          [](const std::string& input_path,
             const std::string& output_path,
             const std::string& viz_path,
             const std::vector<compressor::core::AlgorithmID>& chain,
             size_t stream_chunk_bytes)
              -> compressor::api::CompressResult {
              return compressor::api::compressFileWithViz(
                  input_path, output_path, viz_path, chain, stream_chunk_bytes);
          },
          py::arg("input_path"), py::arg("output_path"), py::arg("viz_path"),
          py::arg("chain"),
          py::arg("stream_chunk_bytes") = size_t{0},
          py::call_guard<py::gil_scoped_release>(),
          "Streaming compress a file with visualization data written to viz_path");
}

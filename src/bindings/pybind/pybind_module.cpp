#include <optional>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include "ICompressor.hpp"
#include "DeflateCompressor.hpp"
#include "LZSSCompressor.hpp"
#include "LZDPCompressor.hpp"
#include "DPFlateCompressor.hpp"
#include "GzipCompressor.hpp"
#include "BrotliCompressor.hpp"
#include "ZstdCompressor.hpp"
#include "Archiver.hpp"
#include "api.hpp"
#include "AlgorithmFactory.hpp"

namespace py = pybind11;
using namespace compressor::core;

void init_ade(py::module_& m);
void init_ea(py::module_& m);

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
        .def("get_max_chain_length", &DeflateCompressor::get_max_chain_length)
        .def("set_use_3hfmtree", &DeflateCompressor::set_use_3hfmtree)
        .def("get_use_3hfmtree", &DeflateCompressor::get_use_3hfmtree)
        .def("set_huffman_chunk_bits", &DeflateCompressor::set_huffman_chunk_bits)
        .def("get_huffman_chunk_bits", &DeflateCompressor::get_huffman_chunk_bits)
        .def("set_huffman_offset_chunk_bits", &DeflateCompressor::set_huffman_offset_chunk_bits)
        .def("set_huffman_length_chunk_bits", &DeflateCompressor::set_huffman_length_chunk_bits)
        .def("get_huffman_offset_chunk_bits", &DeflateCompressor::get_huffman_offset_chunk_bits)
        .def("get_huffman_length_chunk_bits", &DeflateCompressor::get_huffman_length_chunk_bits)
        .def("set_lookahead_size", &DeflateCompressor::set_lookahead_size)
        .def("get_lookahead_size", &DeflateCompressor::get_lookahead_size)
        .def("set_dp_sub_match_max", &DeflateCompressor::set_dp_sub_match_max)
        .def("get_dp_sub_match_max", &DeflateCompressor::get_dp_sub_match_max)
        .def("set_match_engine", &DeflateCompressor::set_match_engine)
        .def("get_match_engine", &DeflateCompressor::get_match_engine)
        .def("set_use_flag_encoding", &DeflateCompressor::set_use_flag_encoding)
        .def("get_use_flag_encoding", &DeflateCompressor::get_use_flag_encoding);

    py::class_<LZSSCompressor, ICompressor,
               std::shared_ptr<LZSSCompressor>>(m, "LZSSCompressor")
        .def(py::init<>())
        .def("set_search_size", &LZSSCompressor::set_search_size)
        .def("get_search_size", &LZSSCompressor::get_search_size)
        .def("set_min_match", &LZSSCompressor::set_min_match)
        .def("get_min_match", &LZSSCompressor::get_min_match)
        .def("set_lookahead_size", &LZSSCompressor::set_lookahead_size)
        .def("get_lookahead_size", &LZSSCompressor::get_lookahead_size)
        .def("set_use_flag_encoding", &LZSSCompressor::set_use_flag_encoding)
        .def("get_use_flag_encoding", &LZSSCompressor::get_use_flag_encoding);

    py::class_<LZDPCompressor, ICompressor,
               std::shared_ptr<LZDPCompressor>>(m, "LZDPCompressor")
        .def(py::init<>())
        .def("set_search_size", &LZDPCompressor::set_search_size)
        .def("get_search_size", &LZDPCompressor::get_search_size)
        .def("set_lookahead_size", &LZDPCompressor::set_lookahead_size)
        .def("get_lookahead_size", &LZDPCompressor::get_lookahead_size)
        .def("set_min_match", &LZDPCompressor::set_min_match)
        .def("get_min_match", &LZDPCompressor::get_min_match)
        .def("set_dp_top", &LZDPCompressor::set_dp_top)
        .def("get_dp_top", &LZDPCompressor::get_dp_top)
        .def("set_dp_depth",
             [](LZDPCompressor& self, size_t v) { self.set_dp_top(v); })
        .def("get_dp_depth",
             [](LZDPCompressor& self) { return self.get_dp_top(); })
        .def("set_dp_range",
             [](LZDPCompressor& self, size_t v) { self.set_dp_top(v); })
        .def("get_dp_range",
             [](LZDPCompressor& self) { return self.get_dp_top(); })
        .def("set_use_flag_encoding", &LZDPCompressor::set_use_flag_encoding)
        .def("get_use_flag_encoding", &LZDPCompressor::get_use_flag_encoding)
        .def("set_match_engine", &LZDPCompressor::set_match_engine)
        .def("get_match_engine", &LZDPCompressor::get_match_engine)
        // range==0 uses compressor's dp_range_ (same knob as GUI「DP优化深度」)
        .def("get_dp_visualization", &LZDPCompressor::get_dp_visualization,
             py::arg("data"), py::arg("range") = 0);

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

    py::class_<DPFlateCompressor, ICompressor,
               std::shared_ptr<DPFlateCompressor>>(m, "DPFlateCompressor")
        .def(py::init<>())
        .def("set_search_size", &DPFlateCompressor::set_search_size)
        .def("get_search_size", &DPFlateCompressor::get_search_size)
        .def("set_lookahead_size", &DPFlateCompressor::set_lookahead_size)
        .def("get_lookahead_size", &DPFlateCompressor::get_lookahead_size)
        .def("set_min_match", &DPFlateCompressor::set_min_match)
        .def("get_min_match", &DPFlateCompressor::get_min_match)
        .def("set_max_chain_length", &DPFlateCompressor::set_max_chain_length)
        .def("get_max_chain_length", &DPFlateCompressor::get_max_chain_length)
        .def("set_dp_depth",
             [](DPFlateCompressor& self, size_t v) { self.set_dp_sub_match_max(v); })
        .def("get_dp_depth",
             [](DPFlateCompressor& self) { return self.get_dp_sub_match_max(); })
        .def("set_dp_sub_match_max", &DPFlateCompressor::set_dp_sub_match_max)
        .def("get_dp_sub_match_max", &DPFlateCompressor::get_dp_sub_match_max)
        .def("set_match_engine", &DPFlateCompressor::set_match_engine)
        .def("get_match_engine", &DPFlateCompressor::get_match_engine)
        .def("set_use_flag_encoding", &DPFlateCompressor::set_use_flag_encoding)
        .def("get_use_flag_encoding", &DPFlateCompressor::get_use_flag_encoding)
        .def("set_use_3hfmtree", &DPFlateCompressor::set_use_3hfmtree)
        .def("get_use_3hfmtree", &DPFlateCompressor::get_use_3hfmtree)
        .def("set_huffman_chunk_bits", &DPFlateCompressor::set_huffman_chunk_bits)
        .def("get_huffman_chunk_bits", &DPFlateCompressor::get_huffman_chunk_bits)
        .def("set_huffman_offset_chunk_bits", &DPFlateCompressor::set_huffman_offset_chunk_bits)
        .def("set_huffman_length_chunk_bits", &DPFlateCompressor::set_huffman_length_chunk_bits)
        .def("get_huffman_offset_chunk_bits", &DPFlateCompressor::get_huffman_offset_chunk_bits)
        .def("get_huffman_length_chunk_bits", &DPFlateCompressor::get_huffman_length_chunk_bits);

    py::class_<GzipCompressor, ICompressor,
               std::shared_ptr<GzipCompressor>>(m, "GzipCompressor")
        .def(py::init<>())
        .def("set_compression_level", &GzipCompressor::set_compression_level)
        .def("get_compression_level", &GzipCompressor::get_compression_level);

    py::class_<BrotliCompressor, ICompressor,
               std::shared_ptr<BrotliCompressor>>(m, "BrotliCompressor")
        .def(py::init<>())
        .def("set_window_size", &BrotliCompressor::set_window_size)
        .def("get_window_size", &BrotliCompressor::get_window_size)
        .def("set_min_match", &BrotliCompressor::set_min_match)
        .def("get_min_match", &BrotliCompressor::get_min_match)
        .def("set_max_chain_length", &BrotliCompressor::set_max_chain_length)
        .def("get_max_chain_length", &BrotliCompressor::get_max_chain_length);

    py::class_<ZstdCompressor, ICompressor,
               std::shared_ptr<ZstdCompressor>>(m, "ZstdCompressor")
        .def(py::init<>())
        .def("set_compression_level", &ZstdCompressor::set_compression_level)
        .def("get_compression_level", &ZstdCompressor::get_compression_level);

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

    py::class_<compressor::core::LzdpWholeFileParams>(m, "LzdpWholeFileParams")
        .def(py::init<>())
        .def_readwrite("search_size", &compressor::core::LzdpWholeFileParams::search_size)
        .def_readwrite("lookahead_size", &compressor::core::LzdpWholeFileParams::lookahead_size)
        .def_readwrite("min_match", &compressor::core::LzdpWholeFileParams::min_match)
        .def_readwrite("dp_top", &compressor::core::LzdpWholeFileParams::dp_top)
        .def_readwrite("use_flag_encoding", &compressor::core::LzdpWholeFileParams::use_flag_encoding)
        .def_readwrite("match_engine", &compressor::core::LzdpWholeFileParams::match_engine);

    py::class_<compressor::core::DpflatePipelineParams>(m, "DpflatePipelineParams")
        .def(py::init<>())
        .def_readwrite("search_size", &compressor::core::DpflatePipelineParams::search_size)
        .def_readwrite("lookahead_size", &compressor::core::DpflatePipelineParams::lookahead_size)
        .def_readwrite("min_match", &compressor::core::DpflatePipelineParams::min_match)
        .def_readwrite("max_chain_length",
                       &compressor::core::DpflatePipelineParams::max_chain_length)
        .def_readwrite("dp_sub_match_max",
                       &compressor::core::DpflatePipelineParams::dp_sub_match_max)
        .def_readwrite("match_engine", &compressor::core::DpflatePipelineParams::match_engine)
        .def_readwrite("use_flag_encoding",
                       &compressor::core::DpflatePipelineParams::use_flag_encoding)
        .def_readwrite("use_3hfmtree",
                       &compressor::core::DpflatePipelineParams::use_3hfmtree)
        .def_readwrite("huffman_offset_chunk_bits",
                       &compressor::core::DpflatePipelineParams::huffman_offset_chunk_bits)
        .def_readwrite("huffman_length_chunk_bits",
                       &compressor::core::DpflatePipelineParams::huffman_length_chunk_bits);

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
             size_t stream_chunk_bytes,
             uint32_t file_compress_opts,
             const std::optional<compressor::core::LzdpWholeFileParams>& lzdp_wf,
             const std::optional<compressor::core::DpflatePipelineParams>& dpflate_p)
              -> compressor::api::CompressResult {
              const compressor::core::LzdpWholeFileParams* p =
                  lzdp_wf.has_value() ? &lzdp_wf.value() : nullptr;
              const compressor::core::DpflatePipelineParams* d =
                  dpflate_p.has_value() ? &dpflate_p.value() : nullptr;
              return compressor::api::compressFile(input_path, output_path, chain,
                                                    stream_chunk_bytes,
                                                    file_compress_opts,
                                                    p, d);
          },
          py::arg("input_path"), py::arg("output_path"), py::arg("chain"),
          py::arg("stream_chunk_bytes") = size_t{0},
          py::arg("file_compress_opts") = uint32_t{0},
          py::arg("lzdp_whole_file") = std::nullopt,
          py::arg("dpflate_pipeline") = std::nullopt,
          py::call_guard<py::gil_scoped_release>(),
          "Streaming compress a file in chunks. stream_chunk_bytes is clamped to 64 KiB–128 MiB "
          "(default 1 MiB when 0); same policy as LZDP/DPFlate pipeline chunk size. "
          "LZDP: LZDP_OutOfCore (chunked window + spill A/B + backtrack + emit); LzdpWholeFileParams. "
          "DPFlate: pass DpflatePipelineParams matching DPFlateCompressor / GUI. "
          "lzdp_whole_file / dpflate_pipeline: optional snapshots (omit when not using that algo).");

    m.def("pipeline_compress_directory",
          [](const std::string& dir_path,
             const std::string& output_path,
             const std::vector<compressor::core::AlgorithmID>& chain,
             size_t stream_chunk_bytes,
             uint32_t file_compress_opts,
             const std::optional<compressor::core::LzdpWholeFileParams>& lzdp_wf,
             const std::optional<compressor::core::DpflatePipelineParams>& dpflate_p)
              -> compressor::api::CompressResult {
              const compressor::core::LzdpWholeFileParams* p =
                  lzdp_wf.has_value() ? &lzdp_wf.value() : nullptr;
              const compressor::core::DpflatePipelineParams* d =
                  dpflate_p.has_value() ? &dpflate_p.value() : nullptr;
              return compressor::api::compressDirectory(dir_path, output_path, chain,
                                                       stream_chunk_bytes,
                                                       file_compress_opts, p, d);
          },
          py::arg("dir_path"), py::arg("output_path"), py::arg("chain"),
          py::arg("stream_chunk_bytes") = size_t{0},
          py::arg("file_compress_opts") = uint32_t{0},
          py::arg("lzdp_whole_file") = std::nullopt,
          py::arg("dpflate_pipeline") = std::nullopt,
          py::call_guard<py::gil_scoped_release>(),
          "Streaming compress a directory tree to one WCX (folder flag); same optional params as "
          "pipeline_compress_file.");

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
          "Streaming decompress a file in chunks (stream_chunk_bytes clamped like compress).");

    m.def("set_streaming_compress_cancel_requested",
          [](bool requested) {
              compressor::api::set_streaming_compress_cancel_requested(requested);
          },
          py::arg("requested"),
          "Request cooperative cancel for pipeline_compress_file (checked between input chunks).");

    // ===== ADE (Algorithm Decision Engine) =====
    init_ade(m);

    // ===== EA (Evolutionary Algorithms) - Parameter Optimizer =====
    init_ea(m);
}

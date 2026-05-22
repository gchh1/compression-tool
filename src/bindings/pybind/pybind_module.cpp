#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include "api.hpp"
#include "AlgorithmFactory.hpp"
#include "LZDP.hpp"
#include "DebugLog.hpp"

namespace py = pybind11;
using namespace compressor::core;

namespace {

void assign_byte_vector_from_buffer(std::vector<uint8_t>& out, const py::object& ob) {
    if (ob.is_none()) {
        out.clear();
        return;
    }
    py::buffer_info info = py::buffer(ob).request();
    if (info.ndim != 1) {
        throw py::value_error("expected 1-dimensional buffer for byte vector");
    }
    const auto* p = static_cast<const uint8_t*>(info.ptr);
    const size_t nbytes =
        static_cast<size_t>(info.shape[0]) * static_cast<size_t>(info.itemsize);
    out.assign(p, p + nbytes);
}

auto vector_to_pybytes(const std::vector<uint8_t>& v) -> py::bytes {
    if (v.empty()) {
        return py::bytes(std::string());
    }
    return py::bytes(reinterpret_cast<const char*>(v.data()), v.size());
}

auto buffer_to_u8vec(py::buffer buf) -> std::vector<uint8_t> {
    py::buffer_info info = buf.request();
    if (info.ndim != 1) {
        throw py::value_error("expected 1-dimensional byte buffer");
    }
    const size_t nbytes =
        static_cast<size_t>(info.shape[0]) * static_cast<size_t>(info.itemsize);
    const auto* p = static_cast<const uint8_t*>(info.ptr);
    return std::vector<uint8_t>(p, p + nbytes);
}

}  // namespace

void init_ade(py::module_& m);
void init_ea(py::module_& m);

PYBIND11_MODULE(core_engine, m) {
    m.doc() = "Web Compressor C++ Core Engine";

    // === DEBUG_BLOCK_BEGIN ===
    if (const char* debug_log_path = std::getenv("WEBCOMPRESS_DEBUG_LOG")) {
        if (debug_log_path[0] != '\0') {
            compressor::debug::DebugLog::instance().enable(debug_log_path);
            DEBUG_LOG("[pybind] WEBCOMPRESS_DEBUG_LOG enabled: %s", debug_log_path);
        }
    }
    m.def("enable_debug_log",
          [](const std::string& path) {
              compressor::debug::DebugLog::instance().enable(path);
              DEBUG_LOG("[pybind] enable_debug_log: %s", path.c_str());
          },
          py::arg("path"),
          "Enable native debug log output.");
    m.def("disable_debug_log",
          []() { compressor::debug::DebugLog::instance().disable(); },
          "Disable native debug log output.");
    // === DEBUG_BLOCK_END ===

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

    // ===== AlgorithmID enum =====

    py::enum_<compressor::core::AlgorithmID>(m, "AlgorithmID")
        .value("NONE", compressor::core::AlgorithmID::None)
        .value("DEFLATE", compressor::core::AlgorithmID::Deflate)
        .value("INFLATE", compressor::core::AlgorithmID::Inflate)
        .value("DELTA_ENCODE", compressor::core::AlgorithmID::DeltaEncode)
        .value("DELTA_DECODE", compressor::core::AlgorithmID::DeltaDecode)
        .value("LZSS", compressor::core::AlgorithmID::LZSS)
        .value("LZSS_DECOMPRESS", compressor::core::AlgorithmID::LZSSDecompress)
        .value("LZSS_NOFLAG", compressor::core::AlgorithmID::LZSS_NoFlag)
        .value("LZSS_DECOMPRESS_NOFLAG", compressor::core::AlgorithmID::LZSSDecompress_NoFlag)
        .value("LZDP", compressor::core::AlgorithmID::LZDP)
        .value("LZDP_DECOMPRESS", compressor::core::AlgorithmID::LZDPDecompress)
        .value("LZMINE", compressor::core::AlgorithmID::LZDP) // Deprecated alias
        .value("LZMINE_DECOMPRESS", compressor::core::AlgorithmID::LZDPDecompress) // Deprecated alias
        .value("DPFLATE", compressor::core::AlgorithmID::DPFlate)
        .value("BROTLI", compressor::core::AlgorithmID::Brotli)
        .value("BROTLI_DECOMPRESS", compressor::core::AlgorithmID::BrotliDecompress)
        .value("ZSTD", compressor::core::AlgorithmID::Zstd)
        .value("ZSTD_DECOMPRESS", compressor::core::AlgorithmID::ZstdDecompress)
        .value("JPEG_COMPRESS", compressor::core::AlgorithmID::JPEG_Compress)
        .value("JPEG_DECOMPRESS", compressor::core::AlgorithmID::JPEG_Decompress)
        .value("WEBP_COMPRESS", compressor::core::AlgorithmID::WebP_Compress)
        .value("WEBP_DECOMPRESS", compressor::core::AlgorithmID::WebP_Decompress)
        .export_values();

    // ===== Parameter struct bindings (kept for backward compat) =====

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

    py::class_<compressor::core::DeflatePipelineParams>(m, "DeflatePipelineParams")
        .def(py::init<>())
        .def_readwrite("search_size", &compressor::core::DeflatePipelineParams::search_size)
        .def_readwrite("lookahead_size",
                       &compressor::core::DeflatePipelineParams::lookahead_size)
        .def_readwrite("min_match", &compressor::core::DeflatePipelineParams::min_match)
        .def_readwrite("max_chain_length",
                       &compressor::core::DeflatePipelineParams::max_chain_length);

    py::class_<compressor::core::ImageCompressParams>(m, "ImageCompressParams")
        .def(py::init<>())
        .def_readwrite("quality", &compressor::core::ImageCompressParams::quality)
        .def_readwrite("max_width", &compressor::core::ImageCompressParams::max_width)
        .def_readwrite("max_height", &compressor::core::ImageCompressParams::max_height);

    // ===== Result structs =====

    py::class_<compressor::api::CompressResult>(m, "PipelineCompressResult")
        .def(py::init<>())
        .def_property(
            "data",
            [](const compressor::api::CompressResult& r) {
                return vector_to_pybytes(r.data);
            },
            [](compressor::api::CompressResult& r, const py::object& ob) {
                assign_byte_vector_from_buffer(r.data, ob);
            })
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
        .def_property(
            "payload",
            [](const compressor::api::WCXUnpackResult& r) {
                return vector_to_pybytes(r.payload);
            },
            [](compressor::api::WCXUnpackResult& r, const py::object& ob) {
                assign_byte_vector_from_buffer(r.payload, ob);
            })
        .def_readwrite("error_message", &compressor::api::WCXUnpackResult::error_message);

    // ===== Config loading =====

    m.def("load_config",
          [](const std::string& config_path) {
              compressor::core::reload_compression_config(config_path);
          },
          py::arg("config_path"),
          "Load compression config from JSON file into C++ global config. "
          "Call at startup and after GUI config changes.");

    m.def("get_config_json",
          []() -> std::string {
              return compressor::core::compression_config().to_json_string();
          },
          "Return the current global compression config as a JSON string.");

    // ===== Core API functions =====

    m.def("pack_wcx",
          [](py::buffer buf,
             compressor::core::AlgorithmID algorithm,
             size_t original_size,
             const std::string& original_filename,
             bool is_folder) -> py::bytes {
              auto packed = compressor::api::pack_wcx(buffer_to_u8vec(buf), algorithm,
                                                       original_size,
                                                       original_filename,
                                                       is_folder);
              return vector_to_pybytes(packed);
          },
          py::arg("compressed_data"),
          py::arg("algorithm"),
          py::arg("original_size"),
          py::arg("original_filename") = "",
          py::arg("is_folder") = false,
          "Pack WCX header + payload");

    m.def("unpack_wcx",
          [](py::buffer buf) -> compressor::api::WCXUnpackResult {
              return compressor::api::unpack_wcx(buffer_to_u8vec(buf));
          },
          py::arg("data"),
          "Unpack WCX header and payload");

    m.def("pipeline_compress",
          [](py::buffer buf,
             const std::vector<compressor::core::AlgorithmID>& chain,
             size_t stream_chunk_bytes,
             const std::string& overrides_json)
              -> compressor::api::CompressResult {
              return compressor::api::compress(buffer_to_u8vec(buf), chain,
                                                stream_chunk_bytes, overrides_json);
          },
          py::arg("data"), py::arg("chain"),
          py::arg("stream_chunk_bytes") = size_t{0},
          py::arg("overrides_json") = std::string{},
          "In-memory compress. Reads params from global config; pass overrides_json for ADE delta.");

    m.def("pipeline_decompress",
          [](py::buffer buf,
             const std::vector<compressor::core::AlgorithmID>& chain,
             size_t stream_chunk_bytes)
              -> compressor::api::CompressResult {
              return compressor::api::decompress(buffer_to_u8vec(buf), chain,
                                                  stream_chunk_bytes);
          },
          py::arg("data"), py::arg("chain"),
          py::arg("stream_chunk_bytes") = size_t{0},
          "In-memory decompress using a pipeline.");

    // Unified streaming file compression (merged compressFile + compressFileWithViz + compressFileWithHeat)
    m.def("pipeline_compress_file",
          [](const std::string& input_path,
             const std::string& output_path,
             const std::vector<compressor::core::AlgorithmID>& chain,
             size_t stream_chunk_bytes,
             const std::string& viz_path,
             const std::string& heat_path,
             const std::string& overrides_json)
              -> compressor::api::CompressResult {
              return compressor::api::compressFile(input_path, output_path, chain,
                                                    stream_chunk_bytes, viz_path,
                                                    heat_path, overrides_json);
          },
          py::arg("input_path"), py::arg("output_path"), py::arg("chain"),
          py::arg("stream_chunk_bytes") = size_t{0},
          py::arg("viz_path") = std::string{},
          py::arg("heat_path") = std::string{},
          py::arg("overrides_json") = std::string{},
          "Streaming compress a file to WCX. When viz_path is set, writes .viz v2 visualization. "
          "When heat_path is set, writes .heat v1 entropy file. "
          "All three outputs are produced in a single streaming pass. "
          "Algorithm params read from global config; pass overrides_json for per-file ADE delta.");

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
          "WCX payload <=256MiB is read fully before decode; larger payloads use chunked reads. "
          "Decompressed output is always written in chunks to disk.");

    m.def("set_streaming_compress_cancel_requested",
          [](bool requested) {
              compressor::api::set_streaming_compress_cancel_requested(requested);
          },
          py::arg("requested"),
          "Request cooperative cancel for pipeline_compress_file (checked between input chunks).");

    py::class_<compressor::api::WcxEntrySummary>(m, "WcxEntrySummary")
        .def(py::init<>())
        .def_readwrite("filename", &compressor::api::WcxEntrySummary::filename)
        .def_readwrite("original_size", &compressor::api::WcxEntrySummary::original_size)
        .def_readwrite("compressed_size", &compressor::api::WcxEntrySummary::compressed_size)
        .def_readwrite("algo_code", &compressor::api::WcxEntrySummary::algo_code);

    m.def("scan_wcx",
          [](const std::string& input_path) -> std::vector<compressor::api::WcxEntrySummary> {
              return compressor::api::scanWcx(input_path);
          },
          py::arg("input_path"),
          "Scan a WCX archive: read all entry headers sequentially without decompressing payloads. "
          "Returns a list of WcxEntrySummary for browse-mode display.");

    // ===== ADE (Algorithm Decision Engine) =====
    init_ade(m);

    // ===== EA (Evolutionary Algorithms) - Parameter Optimizer =====
    init_ea(m);
}

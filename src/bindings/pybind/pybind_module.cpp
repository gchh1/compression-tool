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

    // === DEBUG_BLOCK_BEGIN (可删除) ===
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

    // ===== 流式分块 Pipeline API =====

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
          py::arg("is_folder") = false,          "Pack WCX header + payload");

    m.def("unpack_wcx",
          [](py::buffer buf) -> compressor::api::WCXUnpackResult {
              return compressor::api::unpack_wcx(buffer_to_u8vec(buf));
          },
          py::arg("data"),          "Unpack WCX header and payload");

    m.def("pipeline_compress",
          [](py::buffer buf,
             const std::vector<compressor::core::AlgorithmID>& chain,
             const std::optional<compressor::core::LzdpWholeFileParams>& lzdp_wf,
             const std::optional<compressor::core::DpflatePipelineParams>& dpflate_p,
             const std::optional<compressor::core::DeflatePipelineParams>& deflate_p,
             const std::optional<compressor::core::ImageCompressParams>& image_p,
             size_t stream_chunk_bytes)
              -> compressor::api::CompressResult {
              const compressor::core::LzdpWholeFileParams* p =
                  lzdp_wf.has_value() ? &lzdp_wf.value() : nullptr;
              const compressor::core::DpflatePipelineParams* d =
                  dpflate_p.has_value() ? &dpflate_p.value() : nullptr;
              const compressor::core::DeflatePipelineParams* df =
                  deflate_p.has_value() ? &deflate_p.value() : nullptr;
              const compressor::core::ImageCompressParams* im =
                  image_p.has_value() ? &image_p.value() : nullptr;
              return compressor::api::compress(buffer_to_u8vec(buf), chain, p, d, df, im,
                                                stream_chunk_bytes);
          },
          py::arg("data"), py::arg("chain"),
          py::arg("lzdp_whole_file") = std::nullopt,
          py::arg("dpflate_pipeline") = std::nullopt,
          py::arg("deflate_pipeline") = std::nullopt,
          py::arg("image_compress") = std::nullopt,
          py::arg("stream_chunk_bytes") = size_t{0},
          "Compress data using a pipeline.");

    m.def("pipeline_decompress",
          [](py::buffer buf,
             const std::vector<compressor::core::AlgorithmID>& chain,
             const std::optional<compressor::core::LzdpWholeFileParams>& lzdp_wf,
             const std::optional<compressor::core::DpflatePipelineParams>& dpflate_p,
             const std::optional<compressor::core::DeflatePipelineParams>& deflate_p,
             const std::optional<compressor::core::ImageCompressParams>& image_p,
             size_t stream_chunk_bytes)
              -> compressor::api::CompressResult {
              const compressor::core::LzdpWholeFileParams* p =
                  lzdp_wf.has_value() ? &lzdp_wf.value() : nullptr;
              const compressor::core::DpflatePipelineParams* d =
                  dpflate_p.has_value() ? &dpflate_p.value() : nullptr;
              const compressor::core::DeflatePipelineParams* df =
                  deflate_p.has_value() ? &deflate_p.value() : nullptr;
              const compressor::core::ImageCompressParams* im =
                  image_p.has_value() ? &image_p.value() : nullptr;
              return compressor::api::decompress(buffer_to_u8vec(buf), chain, p, d, df, im,
                                                 stream_chunk_bytes);
          },
          py::arg("data"), py::arg("chain"),
          py::arg("lzdp_whole_file") = std::nullopt,
          py::arg("dpflate_pipeline") = std::nullopt,
          py::arg("deflate_pipeline") = std::nullopt,
          py::arg("image_compress") = std::nullopt,
          py::arg("stream_chunk_bytes") = size_t{0},
          "Decompress data using a pipeline.");

    m.def("pipeline_compress_file",
          [](const std::string& input_path,
             const std::string& output_path,
             const std::vector<compressor::core::AlgorithmID>& chain,
             size_t stream_chunk_bytes,
             uint32_t file_compress_opts,
             const std::optional<compressor::core::LzdpWholeFileParams>& lzdp_wf,
             const std::optional<compressor::core::DpflatePipelineParams>& dpflate_p,
             const std::optional<compressor::core::DeflatePipelineParams>& deflate_p)
              -> compressor::api::CompressResult {
              const compressor::core::LzdpWholeFileParams* p =
                  lzdp_wf.has_value() ? &lzdp_wf.value() : nullptr;
              const compressor::core::DpflatePipelineParams* d =
                  dpflate_p.has_value() ? &dpflate_p.value() : nullptr;
              const compressor::core::DeflatePipelineParams* df =
                  deflate_p.has_value() ? &deflate_p.value() : nullptr;
              return compressor::api::compressFile(input_path, output_path, chain,
                                                    stream_chunk_bytes,
                                                    file_compress_opts,
                                                    p, d, df);
          },
          py::arg("input_path"), py::arg("output_path"), py::arg("chain"),
          py::arg("stream_chunk_bytes") = size_t{0},
          py::arg("file_compress_opts") = uint32_t{0},
          py::arg("lzdp_whole_file") = std::nullopt,
          py::arg("dpflate_pipeline") = std::nullopt,
          py::arg("deflate_pipeline") = std::nullopt,          "Streaming compress a file in chunks. stream_chunk_bytes is clamped to 64 KiB–128 MiB "
          "(default 1 MiB when 0); same policy as LZDP/DPFlate pipeline chunk size. "
          "Deflate: ``algorithm::Deflate`` streaming core (same interaction as DPFlate/LZDP); "
          "optional DeflatePipelineParams (search_size / lookahead_size / min_match / "
          "max_chain_length; lookahead caps LZ match length, max 258 for valid DEFLATE). "
          "LZDP: LzdpWholeFileParams. DPFlate: DpflatePipelineParams.");

    // ============================================================
    // [VIZ] compressFileWithViz — generates .viz file alongside compression
    // ============================================================
    m.def("pipeline_compress_file_with_viz",
          [](const std::string& input_path,
             const std::string& output_path,
             const std::string& viz_path,
             const std::vector<compressor::core::AlgorithmID>& chain,
             size_t stream_chunk_bytes,
             const std::string& heat_path)
              -> compressor::api::CompressResult {
              return compressor::api::compressFileWithViz(input_path, output_path,
                                                            viz_path, chain,
                                                            stream_chunk_bytes,
                                                            heat_path);
          },
          py::arg("input_path"), py::arg("output_path"), py::arg("viz_path"),
          py::arg("chain"),
          py::arg("stream_chunk_bytes") = size_t{0},
          py::arg("heat_path") = std::string{},
          "Compress a file and write visualization events to viz_path (.viz v2 format). "
          "When heat_path is non-empty, also writes per-chunk Shannon entropy to a .heat v1 file. "
          "The compress algorithm must inherit from AlgorithmBase (e.g. DPFlate, Deflate, Brotli).");
    // ============================================================

    // [HEAT] compressFileWithHeat — .heat v1 entropy file alongside compression
    // ============================================================
    m.def("pipeline_compress_file_with_heat",
          [](const std::string& input_path,
             const std::string& output_path,
             const std::string& heat_path,
             const std::vector<compressor::core::AlgorithmID>& chain,
             size_t stream_chunk_bytes)
              -> compressor::api::CompressResult {
              return compressor::api::compressFileWithHeat(input_path, output_path,
                                                           heat_path, chain,
                                                           stream_chunk_bytes);
          },
          py::arg("input_path"), py::arg("output_path"), py::arg("heat_path"),
          py::arg("chain"),
          py::arg("stream_chunk_bytes") = size_t{0},
          "Compress a file and write per-chunk Shannon entropy to heat_path (.heat v1 format). "
          "Works with any algorithm — entropy is computed on raw input chunks before compression.");
    // ============================================================

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
          py::arg("stream_chunk_bytes") = size_t{0},          "WCX payload <=256MiB is read fully before decode; larger payloads use chunked reads. "
          "Decompressed output is always written in chunks to disk (stream_chunk_bytes sizes the pool).");

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

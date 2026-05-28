/// GUI-compatible ``core_engine_new`` module (algorithm_new + api_new + core_new GUI shims).

#include <optional>
#include <stdexcept>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "AlgorithmFactory.hpp"
#include "AudioCompressorBindings.hpp"
#include "GuiCompressors.hpp"
#include "GzipCompressor.hpp"
#include "ImageCompressorBindings.hpp"
#include "ade_debug_log.h"
#include "Visualization.hpp"
#include "api.hpp"

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
        throw py::value_error("expected 1-dimensional buffer");
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

void bind_lzdp_viz(py::module_& m) {
    py::class_<compressor::algorithm::Triple>(m, "LZDPTriple")
        .def_readonly("offset", &compressor::algorithm::Triple::offset)
        .def_readonly("length", &compressor::algorithm::Triple::length)
        .def_readonly("literal", &compressor::algorithm::Triple::literal);

    py::class_<compressor::algorithm::DPCandidate>(m, "LZDPDPCandidate")
        .def_property_readonly(
            "offset",
            [](const compressor::algorithm::DPCandidate& c) { return c.triple.offset; })
        .def_property_readonly(
            "length",
            [](const compressor::algorithm::DPCandidate& c) { return c.triple.length; })
        .def_property_readonly(
            "literal",
            [](const compressor::algorithm::DPCandidate& c) { return c.triple.literal; })
        .def_readonly("is_chosen", &compressor::algorithm::DPCandidate::is_chosen);

    py::class_<compressor::algorithm::DPState>(m, "LZDPDPState")
        .def_readonly("position", &compressor::algorithm::DPState::position)
        .def_readonly("reachable", &compressor::algorithm::DPState::reachable)
        .def_readonly("token_count", &compressor::algorithm::DPState::token_count)
        .def_readonly("cost", &compressor::algorithm::DPState::cost)
        .def_readonly("predecessor", &compressor::algorithm::DPState::predecessor)
        .def_readonly("choice", &compressor::algorithm::DPState::choice);

    py::class_<compressor::algorithm::DPStep>(m, "LZDPDPStep")
        .def_readonly("position", &compressor::algorithm::DPStep::position)
        .def_readonly("candidates", &compressor::algorithm::DPStep::candidates)
        .def_readonly("best_cost", &compressor::algorithm::DPStep::best_cost)
        .def_readonly("best_token_count", &compressor::algorithm::DPStep::best_token_count);

    py::class_<compressor::algorithm::DPVisualization>(m, "LZDPDPVisualization")
        .def_readonly("steps", &compressor::algorithm::DPVisualization::steps)
        .def_readonly("dp_array", &compressor::algorithm::DPVisualization::dp_array)
        .def_readonly("optimal_path", &compressor::algorithm::DPVisualization::optimal_path)
        .def_readonly("input_length", &compressor::algorithm::DPVisualization::input_length)
        .def_readonly("search_size", &compressor::algorithm::DPVisualization::search_size)
        .def_readonly("lookahead_size", &compressor::algorithm::DPVisualization::lookahead_size);
}

void bind_compressors(py::module_& m) {
    py::class_<CompressorResult>(m, "CompressorResult")
        .def(py::init<>())
        .def_property(
            "data",
            [](const CompressorResult& r) { return vector_to_pybytes(r.data); },
            [](CompressorResult& r, const py::object& ob) {
                assign_byte_vector_from_buffer(r.data, ob);
            })
        .def_readwrite("original_size", &CompressorResult::original_size)
        .def_readwrite("compressed_size", &CompressorResult::compressed_size)
        .def_readwrite("compression_ratio", &CompressorResult::compression_ratio)
        .def_readwrite("time_ms", &CompressorResult::time_ms)
        .def_readwrite("success", &CompressorResult::success)
        .def_readwrite("error_message", &CompressorResult::error_message);

    py::class_<ICompressor, std::shared_ptr<ICompressor>>(m, "ICompressor")
        .def(
            "compress",
            [](ICompressor& self, py::buffer buf) { return self.compress(buffer_to_u8vec(buf)); })
        .def(
            "decompress",
            [](ICompressor& self, py::buffer buf) {
                return self.decompress(buffer_to_u8vec(buf));
            })
        .def("get_algorithm_name", &ICompressor::get_algorithm_name);

    py::class_<LZDPCompressor, ICompressor, std::shared_ptr<LZDPCompressor>>(m, "LZDPCompressor")
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
        .def("get_dp_depth", [](LZDPCompressor& self) { return self.get_dp_top(); })
        .def("set_dp_range",
             [](LZDPCompressor& self, size_t v) { self.set_dp_top(v); })
        .def("get_dp_range", [](LZDPCompressor& self) { return self.get_dp_top(); })
        .def("set_use_flag_encoding", &LZDPCompressor::set_use_flag_encoding)
        .def("get_use_flag_encoding", &LZDPCompressor::get_use_flag_encoding)
        .def("set_match_engine", &LZDPCompressor::set_match_engine)
        .def("get_match_engine", &LZDPCompressor::get_match_engine)
        .def(
            "get_dp_visualization",
            [](LZDPCompressor& self, py::buffer buf, size_t range) {
                return self.get_dp_visualization(buffer_to_u8vec(buf), range);
            },
            py::arg("data"), py::arg("range") = 0)
        .def(
            "compress",
            [](LZDPCompressor& self, py::buffer buf) { return self.compress(buffer_to_u8vec(buf)); })
        .def(
            "decompress",
            [](LZDPCompressor& self, py::buffer buf) {
                return self.decompress(buffer_to_u8vec(buf));
            });

    py::class_<LZSSCompressor, ICompressor, std::shared_ptr<LZSSCompressor>>(m, "LZSSCompressor")
        .def(py::init<>())
        .def("set_search_size", &LZSSCompressor::set_search_size)
        .def("get_search_size", &LZSSCompressor::get_search_size)
        .def("set_min_match", &LZSSCompressor::set_min_match)
        .def("get_min_match", &LZSSCompressor::get_min_match)
        .def("set_lookahead_size", &LZSSCompressor::set_lookahead_size)
        .def("get_lookahead_size", &LZSSCompressor::get_lookahead_size)
        .def("set_use_flag_encoding", &LZSSCompressor::set_use_flag_encoding)
        .def("get_use_flag_encoding", &LZSSCompressor::get_use_flag_encoding)
        .def(
            "compress",
            [](LZSSCompressor& self, py::buffer buf) { return self.compress(buffer_to_u8vec(buf)); })
        .def(
            "decompress",
            [](LZSSCompressor& self, py::buffer buf) {
                return self.decompress(buffer_to_u8vec(buf));
            });

    py::class_<DeflateCompressor, ICompressor, std::shared_ptr<DeflateCompressor>>(m,
                                                                                   "DeflateCompressor")
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
        .def("get_use_flag_encoding", &DeflateCompressor::get_use_flag_encoding)
        .def(
            "compress",
            [](DeflateCompressor& self, py::buffer buf) {
                return self.compress(buffer_to_u8vec(buf));
            })
        .def(
            "compress_for_demo",
            [](DeflateCompressor& self, py::buffer buf) {
                return self.compress_for_demo(buffer_to_u8vec(buf));
            })
        .def(
            "decompress",
            [](DeflateCompressor& self, py::buffer buf) {
                return self.decompress(buffer_to_u8vec(buf));
            });

    py::class_<DPFlateCompressor, ICompressor, std::shared_ptr<DPFlateCompressor>>(m,
                                                                                 "DPFlateCompressor")
        .def(py::init<>())
        .def("set_search_size", &DPFlateCompressor::set_search_size)
        .def("get_search_size", &DPFlateCompressor::get_search_size)
        .def("set_lookahead_size", &DPFlateCompressor::set_lookahead_size)
        .def("get_lookahead_size", &DPFlateCompressor::get_lookahead_size)
        .def("set_min_match", &DPFlateCompressor::set_min_match)
        .def("get_min_match", &DPFlateCompressor::get_min_match)
        .def("set_max_chain_length", &DPFlateCompressor::set_max_chain_length)
        .def("get_max_chain_length", &DPFlateCompressor::get_max_chain_length)
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
        .def("get_huffman_length_chunk_bits", &DPFlateCompressor::get_huffman_length_chunk_bits)
        .def(
            "compress",
            [](DPFlateCompressor& self, py::buffer buf) {
                return self.compress(buffer_to_u8vec(buf));
            })
        .def(
            "compress_for_demo",
            [](DPFlateCompressor& self, py::buffer buf) {
                return self.compress_for_demo(buffer_to_u8vec(buf));
            })
        .def(
            "decompress",
            [](DPFlateCompressor& self, py::buffer buf) {
                return self.decompress(buffer_to_u8vec(buf));
            });

    py::class_<GzipCompressor, ICompressor, std::shared_ptr<GzipCompressor>>(m, "GzipCompressor")
        .def(py::init<>())
        .def("set_compression_level", &GzipCompressor::set_compression_level)
        .def("get_compression_level", &GzipCompressor::get_compression_level)
        .def(
            "compress",
            [](GzipCompressor& self, py::buffer buf) {
                return self.compress(buffer_to_u8vec(buf));
            })
        .def(
            "decompress",
            [](GzipCompressor& self, py::buffer buf) {
                return self.decompress(buffer_to_u8vec(buf));
            });

    py::class_<BrotliCompressor, ICompressor, std::shared_ptr<BrotliCompressor>>(
        m, "BrotliCompressor")
        .def(py::init<>())
        .def("set_window_size", &BrotliCompressor::set_window_size)
        .def("get_window_size", &BrotliCompressor::get_window_size)
        .def("set_min_match", &BrotliCompressor::set_min_match)
        .def("get_min_match", &BrotliCompressor::get_min_match)
        .def("set_max_chain_length", &BrotliCompressor::set_max_chain_length)
        .def("get_max_chain_length", &BrotliCompressor::get_max_chain_length)
        .def(
            "compress",
            [](BrotliCompressor& self, py::buffer buf) {
                return self.compress(buffer_to_u8vec(buf));
            })
        .def(
            "decompress",
            [](BrotliCompressor& self, py::buffer buf) {
                return self.decompress(buffer_to_u8vec(buf));
            });

    py::class_<ZstdCompressor, ICompressor, std::shared_ptr<ZstdCompressor>>(
        m, "ZstdCompressor")
        .def(py::init<>())
        .def("set_compression_level", &ZstdCompressor::set_compression_level)
        .def("get_compression_level", &ZstdCompressor::get_compression_level)
        .def(
            "compress",
            [](ZstdCompressor& self, py::buffer buf) {
                return self.compress(buffer_to_u8vec(buf));
            })
        .def(
            "decompress",
            [](ZstdCompressor& self, py::buffer buf) {
                return self.decompress(buffer_to_u8vec(buf));
            });

    py::class_<ImageJpegCompressor, ICompressor, std::shared_ptr<ImageJpegCompressor>>(
        m, "ImageJpegCompressor")
        .def(py::init<>())
        .def("set_quality", &ImageJpegCompressor::set_quality)
        .def("get_quality", &ImageJpegCompressor::get_quality)
        .def(
            "compress",
            [](ImageJpegCompressor& self, py::buffer buf) {
                return self.compress(buffer_to_u8vec(buf));
            })
        .def(
            "decompress",
            [](ImageJpegCompressor& self, py::buffer buf) {
                return self.decompress(buffer_to_u8vec(buf));
            });

    py::class_<ImagePngCompressor, ICompressor, std::shared_ptr<ImagePngCompressor>>(
        m, "ImagePngCompressor")
        .def(py::init<>())
        .def(
            "compress",
            [](ImagePngCompressor& self, py::buffer buf) {
                return self.compress(buffer_to_u8vec(buf));
            })
        .def(
            "decompress",
            [](ImagePngCompressor& self, py::buffer buf) {
                return self.decompress(buffer_to_u8vec(buf));
            });

    py::class_<AudioFlacCompressor, ICompressor, std::shared_ptr<AudioFlacCompressor>>(
        m, "AudioFlacCompressor")
        .def(py::init<>())
        .def("set_quality", &AudioFlacCompressor::set_quality)
        .def("get_quality", &AudioFlacCompressor::get_quality)
        .def(
            "compress",
            [](AudioFlacCompressor& self, py::buffer buf) {
                return self.compress(buffer_to_u8vec(buf));
            })
        .def(
            "decompress",
            [](AudioFlacCompressor& self, py::buffer buf) {
                return self.decompress(buffer_to_u8vec(buf));
            });

    ; // ── AudioAacCompressor removed ──

}

void bind_pipeline(py::module_& m) {
    py::enum_<compressor::core::AlgorithmID>(m, "AlgorithmID")
        .value("NONE", compressor::core::AlgorithmID::None)
        .value("DEFLATE", compressor::core::AlgorithmID::Deflate)
        .value("INFLATE", compressor::core::AlgorithmID::Inflate)
        .value("LZSS", compressor::core::AlgorithmID::LZSS)
        .value("LZSS_DECOMPRESS", compressor::core::AlgorithmID::LZSSDecompress)
        .value("LZDP", compressor::core::AlgorithmID::LZDP)
        .value("LZDP_DECOMPRESS", compressor::core::AlgorithmID::LZDPDecompress)
        .value("LZMINE", compressor::core::AlgorithmID::LZDP)
        .value("LZMINE_DECOMPRESS", compressor::core::AlgorithmID::LZDPDecompress)
        .value("DPFLATE", compressor::core::AlgorithmID::DPFlate)
        .value("BROTLI", compressor::core::AlgorithmID::Brotli)
        .value("BROTLI_DECOMPRESS", compressor::core::AlgorithmID::BrotliDecompress)
        .value("ZSTD", compressor::core::AlgorithmID::Zstd)
        .value("ZSTD_DECOMPRESS", compressor::core::AlgorithmID::ZstdDecompress)
        .value("IMAGE_JPEG", compressor::core::AlgorithmID::ImageJpeg)
        .value("IMAGE_PNG", compressor::core::AlgorithmID::ImagePng)
        .value("IMAGE_JPEG_DECOMPRESS", compressor::core::AlgorithmID::ImageJpegDecompress)
        .value("IMAGE_PNG_DECOMPRESS", compressor::core::AlgorithmID::ImagePngDecompress)
        .value("AUDIO_FLAC", compressor::core::AlgorithmID::AudioFlac)
        .value("AUDIO_FLAC_DECOMPRESS", compressor::core::AlgorithmID::AudioFlacDecompress)
        .export_values();

    py::class_<compressor::core::LzdpWholeFileParams>(m, "LzdpWholeFileParams")
        .def(py::init<>())
        .def_readwrite("search_size", &compressor::core::LzdpWholeFileParams::search_size)
        .def_readwrite("lookahead_size", &compressor::core::LzdpWholeFileParams::lookahead_size)
        .def_readwrite("min_match", &compressor::core::LzdpWholeFileParams::min_match)
        .def_readwrite("dp_top", &compressor::core::LzdpWholeFileParams::dp_top)
        .def_readwrite("use_flag_encoding",
                       &compressor::core::LzdpWholeFileParams::use_flag_encoding)
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
        .def_readwrite("use_3hfmtree", &compressor::core::DpflatePipelineParams::use_3hfmtree)
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
                       &compressor::core::DeflatePipelineParams::max_chain_length)
        .def_readwrite("use_flag_encoding",
                       &compressor::core::DeflatePipelineParams::use_flag_encoding)
        .def_readwrite("use_3hfmtree",
                       &compressor::core::DeflatePipelineParams::use_3hfmtree)
        .def_readwrite("huffman_offset_chunk_bits",
                       &compressor::core::DeflatePipelineParams::huffman_offset_chunk_bits)
        .def_readwrite("huffman_length_chunk_bits",
                       &compressor::core::DeflatePipelineParams::huffman_length_chunk_bits);

    py::class_<compressor::core::LzssPipelineParams>(m, "LzssPipelineParams")
        .def(py::init<>())
        .def_readwrite("search_size", &compressor::core::LzssPipelineParams::search_size)
        .def_readwrite("lookahead_size",
                       &compressor::core::LzssPipelineParams::lookahead_size)
        .def_readwrite("min_match", &compressor::core::LzssPipelineParams::min_match)
        .def_readwrite("use_flag_encoding",
                       &compressor::core::LzssPipelineParams::use_flag_encoding);

    py::class_<compressor::api_new::CompressResult>(m, "PipelineCompressResult")
        .def(py::init<>())
        .def_property(
            "data",
            [](const compressor::api_new::CompressResult& r) { return vector_to_pybytes(r.data); },
            [](compressor::api_new::CompressResult& r, const py::object& ob) {
                assign_byte_vector_from_buffer(r.data, ob);
            })
        .def_readwrite("original_size", &compressor::api_new::CompressResult::original_size)
        .def_readwrite("compressed_size", &compressor::api_new::CompressResult::compressed_size)
        .def_readwrite("compression_ratio", &compressor::api_new::CompressResult::compression_ratio)
        .def_readwrite("time_ms", &compressor::api_new::CompressResult::time_ms)
        .def_readwrite("success", &compressor::api_new::CompressResult::success)
        .def_readwrite("error_message", &compressor::api_new::CompressResult::error_message)
        .def_readwrite("bytes_processed", &compressor::api_new::CompressResult::bytes_processed)
        .def_readwrite("cancelled", &compressor::api_new::CompressResult::cancelled);

    py::class_<compressor::api_new::WCXUnpackResult>(m, "WCXUnpackResult")
        .def(py::init<>())
        .def_readwrite("success", &compressor::api_new::WCXUnpackResult::success)
        .def_readwrite("algo_code", &compressor::api_new::WCXUnpackResult::algo_code)
        .def_readwrite("original_size", &compressor::api_new::WCXUnpackResult::original_size)
        .def_readwrite("compressed_size", &compressor::api_new::WCXUnpackResult::compressed_size)
        .def_readwrite("is_folder", &compressor::api_new::WCXUnpackResult::is_folder)
        .def_readwrite("web_dict_preprocess",
                       &compressor::api_new::WCXUnpackResult::web_dict_preprocess)
        .def_readwrite("original_filename",
                       &compressor::api_new::WCXUnpackResult::original_filename)
        .def_property(
            "payload",
            [](const compressor::api_new::WCXUnpackResult& r) { return vector_to_pybytes(r.payload); },
            [](compressor::api_new::WCXUnpackResult& r, const py::object& ob) {
                assign_byte_vector_from_buffer(r.payload, ob);
            })
        .def_readwrite("error_message", &compressor::api_new::WCXUnpackResult::error_message);

    m.def(
        "pack_wcx",
        [](py::buffer buf, compressor::core::AlgorithmID algorithm, size_t original_size,
           const std::string& original_filename, bool is_folder, bool web_dict_preprocess) -> py::bytes {
            auto data = buffer_to_u8vec(buf);
            std::vector<uint8_t> result;
            {
                py::gil_scoped_release release;
                result = compressor::api_new::pack_wcx(
                    data, algorithm, original_size, original_filename, is_folder,
                    web_dict_preprocess);
            }
            return vector_to_pybytes(result);
        },
        py::arg("compressed_data"), py::arg("algorithm"), py::arg("original_size"),
        py::arg("original_filename") = "", py::arg("is_folder") = false,
        py::arg("web_dict_preprocess") = false);

    m.def(
        "unpack_wcx",
        [](py::buffer buf) -> compressor::api_new::WCXUnpackResult {
            auto data = buffer_to_u8vec(buf);
            py::gil_scoped_release release;
            return compressor::api_new::unpack_wcx(data);
        },
        py::arg("data"));

    m.def(
        "pipeline_compress",
        [](py::buffer buf, const std::vector<compressor::core::AlgorithmID>& chain,
           const std::optional<compressor::core::LzdpWholeFileParams>& lzdp_wf,
           const std::optional<compressor::core::DpflatePipelineParams>& dpflate_p,
           const std::optional<compressor::core::DeflatePipelineParams>& deflate_p,
           const std::optional<compressor::core::LzssPipelineParams>& lzss_p,
           size_t stream_chunk_bytes) -> compressor::api_new::CompressResult {
            const compressor::core::LzdpWholeFileParams* p =
                lzdp_wf.has_value() ? &lzdp_wf.value() : nullptr;
            const compressor::core::DpflatePipelineParams* d =
                dpflate_p.has_value() ? &dpflate_p.value() : nullptr;
            const compressor::core::DeflatePipelineParams* df =
                deflate_p.has_value() ? &deflate_p.value() : nullptr;
            const compressor::core::LzssPipelineParams* ls =
                lzss_p.has_value() ? &lzss_p.value() : nullptr;
            auto data = buffer_to_u8vec(buf);
            compressor::ade::ade_debug_writef(
                "pybind pipeline_compress: chain_size=%zu id=%d data_size=%zu lzdp_wf=%d",
                chain.size(), chain.empty() ? -1 : static_cast<int>(chain[0]),
                data.size(), lzdp_wf.has_value() ? 1 : 0);
            py::gil_scoped_release release;
            return compressor::api_new::compress(data, chain, p, d, df, ls,
                                                 stream_chunk_bytes);
        },
        py::arg("data"), py::arg("chain"), py::arg("lzdp_whole_file") = std::nullopt,
        py::arg("dpflate_pipeline") = std::nullopt, py::arg("deflate_pipeline") = std::nullopt,
        py::arg("lzss_pipeline") = std::nullopt,
        py::arg("stream_chunk_bytes") = size_t{0});

    m.def(
        "pipeline_decompress",
        [](py::buffer buf, const std::vector<compressor::core::AlgorithmID>& chain,
           const std::optional<compressor::core::LzdpWholeFileParams>& lzdp_wf,
           const std::optional<compressor::core::DpflatePipelineParams>& dpflate_p,
           const std::optional<compressor::core::DeflatePipelineParams>& deflate_p,
           const std::optional<compressor::core::LzssPipelineParams>& lzss_p,
           size_t stream_chunk_bytes) -> compressor::api_new::CompressResult {
            const compressor::core::LzdpWholeFileParams* p =
                lzdp_wf.has_value() ? &lzdp_wf.value() : nullptr;
            const compressor::core::DpflatePipelineParams* d =
                dpflate_p.has_value() ? &dpflate_p.value() : nullptr;
            const compressor::core::DeflatePipelineParams* df =
                deflate_p.has_value() ? &deflate_p.value() : nullptr;
            const compressor::core::LzssPipelineParams* ls =
                lzss_p.has_value() ? &lzss_p.value() : nullptr;
            auto data = buffer_to_u8vec(buf);
            py::gil_scoped_release release;
            return compressor::api_new::decompress(data, chain, p, d, df, ls,
                                                   stream_chunk_bytes);
        },
        py::arg("data"), py::arg("chain"), py::arg("lzdp_whole_file") = std::nullopt,
        py::arg("dpflate_pipeline") = std::nullopt, py::arg("deflate_pipeline") = std::nullopt,
        py::arg("lzss_pipeline") = std::nullopt,
        py::arg("stream_chunk_bytes") = size_t{0});

    m.def(
        "pipeline_compress_file",
        [](const std::string& input_path, const std::string& output_path,
           const std::vector<compressor::core::AlgorithmID>& chain, size_t stream_chunk_bytes,
           uint32_t file_compress_opts,
           const std::optional<compressor::core::LzdpWholeFileParams>& lzdp_wf,
           const std::optional<compressor::core::DpflatePipelineParams>& dpflate_p,
           const std::optional<compressor::core::DeflatePipelineParams>& deflate_p,
           const std::optional<compressor::core::LzssPipelineParams>& lzss_p) -> compressor::api_new::CompressResult {
            const compressor::core::LzdpWholeFileParams* p =
                lzdp_wf.has_value() ? &lzdp_wf.value() : nullptr;
            const compressor::core::DpflatePipelineParams* d =
                dpflate_p.has_value() ? &dpflate_p.value() : nullptr;
            const compressor::core::DeflatePipelineParams* df =
                deflate_p.has_value() ? &deflate_p.value() : nullptr;
            const compressor::core::LzssPipelineParams* ls =
                lzss_p.has_value() ? &lzss_p.value() : nullptr;
            py::gil_scoped_release release;
            return compressor::api_new::compressFile(input_path, output_path, chain,
                                                     stream_chunk_bytes, file_compress_opts, p,
                                                     d, df, ls);
        },
        py::arg("input_path"), py::arg("output_path"), py::arg("chain"),
        py::arg("stream_chunk_bytes") = size_t{0}, py::arg("file_compress_opts") = uint32_t{0},
        py::arg("lzdp_whole_file") = std::nullopt, py::arg("dpflate_pipeline") = std::nullopt,
        py::arg("deflate_pipeline") = std::nullopt, py::arg("lzss_pipeline") = std::nullopt);

    m.def(
        "pipeline_decompress_file",
        [](const std::string& input_path, const std::string& output_path,
           const std::vector<compressor::core::AlgorithmID>& chain, size_t stream_chunk_bytes) -> compressor::api_new::CompressResult {
            py::gil_scoped_release release;
            return compressor::api_new::decompressFile(input_path, output_path, chain,
                                                       stream_chunk_bytes);
        },
        py::arg("input_path"), py::arg("output_path"), py::arg("chain"),
        py::arg("stream_chunk_bytes") = size_t{0});

    m.def(
        "set_streaming_compress_cancel_requested",
        [](bool requested) -> void {
            fprintf(stderr, "[CANCEL_TRACE] pybind::set_streaming_compress_cancel_requested(%d)\n", requested);
            fflush(stderr);
            compressor::api_new::set_streaming_compress_cancel_requested(requested);
        },
        py::arg("requested"));
}

}  // namespace

void init_ade(py::module_& m);
void init_param_regressor(py::module_& m);
void init_ea(py::module_& m);

PYBIND11_MODULE(core_engine_new, m) {
    m.doc() = "Web Compressor C++ Core Engine (algorithm_new brick stack)";

    bind_lzdp_viz(m);
    bind_compressors(m);
    bind_pipeline(m);

    init_ade(m);
    init_param_regressor(m);
    init_ea(m);

    m.def("test_algorithm_new", []() -> bool { return true; }, "Smoke test: module loaded");
}

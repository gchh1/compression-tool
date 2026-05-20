// [BRIDGE] algorithm_new pybind bindings (brick architecture)
// Binds all New* compressors, config structs, and visualization types
// GUI uses these instead of legacy core:: types

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "Visualization.hpp"
#include "LZencoding.hpp"
#include "Models.hpp"
#include "config/Config.hpp"
#include "LZDP.hpp"
#include "LZSS.hpp"
#include "Deflate.hpp"
#include "Dpflate.hpp"
// [BRIDGE] core_new compressors
#include "../../core_new/include/LZDPCompressor.hpp"
#include "../../core_new/include/LZSScompressor.hpp"
#include "../../core_new/include/Deflatecompressor.hpp"
#include "../../core_new/include/DPflatecompressor.hpp"
#include "../../core_new/include/StreamingCancel.hpp"

namespace py = pybind11;

void init_visualization_new(py::module_& m) {

    // ═══════════════════════════════════════════════
    //  § Brick Config Structs (readwrite for GUI)
    // ═══════════════════════════════════════════════

    py::enum_<compressor::algorithm::models::MatchEngine>(m, "MatchEngine")
        .value("KMP", compressor::algorithm::models::MatchEngine::KMP)
        .value("HashChain", compressor::algorithm::models::MatchEngine::HashChain);

    py::class_<compressor::algorithm::config::Lz77WindowConfig>(m, "Lz77WindowConfig")
        .def(py::init<size_t, size_t, size_t>(),
             py::arg("search_size") = 4095,
             py::arg("look_size") = 255,
             py::arg("min_match_len") = 0)
        .def_readwrite("search_size", &compressor::algorithm::config::Lz77WindowConfig::search_size)
        .def_readwrite("look_size", &compressor::algorithm::config::Lz77WindowConfig::look_size)
        .def_readwrite("min_match_len", &compressor::algorithm::config::Lz77WindowConfig::min_match_len);

    py::class_<compressor::algorithm::config::DpMatcherConfig>(m, "DpMatcherConfig")
        .def(py::init<size_t, compressor::algorithm::models::MatchEngine>(),
             py::arg("dp_top") = 3,
             py::arg("match_engine") = compressor::algorithm::models::MatchEngine::HashChain)
        .def_readwrite("dp_top", &compressor::algorithm::config::DpMatcherConfig::dp_top)
        .def_readwrite("match_engine", &compressor::algorithm::config::DpMatcherConfig::match_engine);

    py::class_<compressor::algorithm::config::HuffmanBackendConfig>(m, "HuffmanBackendConfig")
        .def(py::init<bool, uint8_t, uint8_t>(),
             py::arg("use_3hfmtree") = false,
             py::arg("huffman_offset_bitwidth") = 8,
             py::arg("huffman_length_bitwidth") = 8)
        .def_readwrite("use_3hfmtree", &compressor::algorithm::config::HuffmanBackendConfig::use_3hfmtree)
        .def_readwrite("huffman_offset_bitwidth",
                        &compressor::algorithm::config::HuffmanBackendConfig::huffman_offset_bitwidth)
        .def_readwrite("huffman_length_bitwidth",
                        &compressor::algorithm::config::HuffmanBackendConfig::huffman_length_bitwidth);

    py::class_<compressor::algorithm::EncodingConfig>(m, "EncodingConfig")
        .def_readwrite("offset_bits", &compressor::algorithm::EncodingConfig::offset_bits)
        .def_readwrite("length_bits", &compressor::algorithm::EncodingConfig::length_bits)
        .def_readwrite("use_flag_encoding", &compressor::algorithm::EncodingConfig::use_flag_encoding);

    // ── Algorithm Configs (composed bricks) ──

    py::class_<compressor::algorithm::LZDPConfig>(m, "LZDPConfig")
        .def(py::init<size_t, size_t, size_t,
                      compressor::algorithm::models::MatchEngine, bool, size_t>(),
             py::arg("search_size") = 4095,
             py::arg("lookahead_size") = 255,
             py::arg("dp_top") = 3,
             py::arg("match_engine") = compressor::algorithm::models::MatchEngine::HashChain,
             py::arg("use_flag_encoding") = true,
             py::arg("min_match") = 0)
        .def_readwrite("window", &compressor::algorithm::LZDPConfig::window)
        .def_readwrite("dp", &compressor::algorithm::LZDPConfig::dp)
        .def_readwrite("encoding", &compressor::algorithm::LZDPConfig::encoding);

    py::class_<compressor::algorithm::LZSSConfig>(m, "LZSSConfig")
        .def(py::init<size_t, size_t, size_t, bool>(),
             py::arg("search_size") = 4095,
             py::arg("lookahead_size") = 255,
             py::arg("min_match") = 3,
             py::arg("use_flag_encoding") = true)
        .def_readwrite("window", &compressor::algorithm::LZSSConfig::window)
        .def_readwrite("encoding", &compressor::algorithm::LZSSConfig::encoding);

    py::class_<compressor::algorithm::DeflateConfig>(m, "DeflateConfig")
        .def(py::init<size_t, size_t, size_t, bool, uint8_t, uint8_t, bool>(),
             py::arg("search_size") = 16384,
             py::arg("lookahead_size") = 258,
             py::arg("min_match") = 3,
             py::arg("use_3hfmtree") = false,
             py::arg("huffman_offset_chunk_bits") = 8,
             py::arg("huffman_length_chunk_bits") = 8,
             py::arg("use_flag_encoding") = true)
        .def_readwrite("window", &compressor::algorithm::DeflateConfig::window)
        .def_readwrite("huffman", &compressor::algorithm::DeflateConfig::huffman)
        .def_readwrite("encoding", &compressor::algorithm::DeflateConfig::encoding);

    py::class_<compressor::algorithm::DPflateConfig>(m, "DPflateConfig")
        .def_readwrite("window", &compressor::algorithm::DPflateConfig::window)
        .def_readwrite("dp", &compressor::algorithm::DPflateConfig::dp)
        .def_readwrite("huffman", &compressor::algorithm::DPflateConfig::huffman)
        .def_readwrite("encoding", &compressor::algorithm::DPflateConfig::encoding)
        .def_readwrite("min_match_len", &compressor::algorithm::DPflateConfig::min_match_len);

    // ═══════════════════════════════════════════════
    //  § Visualization Data Structures
    // ═══════════════════════════════════════════════

    py::class_<compressor::algorithm::Triple>(m, "NewTriple")
        .def_readonly("offset", &compressor::algorithm::Triple::offset)
        .def_readonly("length", &compressor::algorithm::Triple::length)
        .def_readonly("literal", &compressor::algorithm::Triple::literal);

    py::class_<compressor::algorithm::DPCandidate>(m, "NewDPCandidate")
        .def_readonly("triple", &compressor::algorithm::DPCandidate::triple)
        .def_readonly("is_chosen", &compressor::algorithm::DPCandidate::is_chosen);

    py::class_<compressor::algorithm::DPState>(m, "NewDPState")
        .def_readonly("position", &compressor::algorithm::DPState::position)
        .def_readonly("reachable", &compressor::algorithm::DPState::reachable)
        .def_readonly("cost", &compressor::algorithm::DPState::cost)
        .def_readonly("token_count", &compressor::algorithm::DPState::token_count)
        .def_readonly("literal_count", &compressor::algorithm::DPState::literal_count)
        .def_readonly("match_count", &compressor::algorithm::DPState::match_count)
        .def_readonly("predecessor", &compressor::algorithm::DPState::predecessor)
        .def_readonly("choice", &compressor::algorithm::DPState::choice);

    py::class_<compressor::algorithm::DPStep>(m, "NewDPStep")
        .def_readonly("position", &compressor::algorithm::DPStep::position)
        .def_readonly("candidates", &compressor::algorithm::DPStep::candidates)
        .def_readonly("best_cost", &compressor::algorithm::DPStep::best_cost);

    py::class_<compressor::algorithm::DPVisualization>(m, "NewDPVisualization")
        .def_readonly("steps", &compressor::algorithm::DPVisualization::steps)
        .def_readonly("dp_array", &compressor::algorithm::DPVisualization::dp_array)
        .def_readonly("optimal_path", &compressor::algorithm::DPVisualization::optimal_path)
        .def_readonly("input_length", &compressor::algorithm::DPVisualization::input_length)
        .def_readonly("search_size", &compressor::algorithm::DPVisualization::search_size)
        .def_readonly("lookahead_size", &compressor::algorithm::DPVisualization::lookahead_size);

    // ═══════════════════════════════════════════════
    //  § CompressorResult (brick style)
    // ═══════════════════════════════════════════════

    py::class_<compressor::algorithm::pipeline::LZDPNonStreamingResult>(m, "LZDPNonStreamingResult")
        .def_readwrite("compressed", &compressor::algorithm::pipeline::LZDPNonStreamingResult::compressed)
        .def_readwrite("triples", &compressor::algorithm::pipeline::LZDPNonStreamingResult::triples);

    py::class_<compressor::algorithm::pipeline::LZSSNonStreamingResult>(m, "LZSSNonStreamingResult")
        .def_readwrite("compressed", &compressor::algorithm::pipeline::LZSSNonStreamingResult::compressed)
        .def_readwrite("triples", &compressor::algorithm::pipeline::LZSSNonStreamingResult::triples);

    py::class_<compressor::algorithm::pipeline::DeflateNonStreamingResult>(m, "DeflateNonStreamingResult")
        .def_readwrite("compressed", &compressor::algorithm::pipeline::DeflateNonStreamingResult::compressed)
        .def_readwrite("triples", &compressor::algorithm::pipeline::DeflateNonStreamingResult::triples);

    py::class_<compressor::algorithm::pipeline::DPFlateNonStreamingResult>(m, "DPFlateNonStreamingResult")
        .def_readwrite("compressed", &compressor::algorithm::pipeline::DPFlateNonStreamingResult::compressed)
        .def_readwrite("triples", &compressor::algorithm::pipeline::DPFlateNonStreamingResult::triples);

    // ═══════════════════════════════════════════════
    //  § New* Compressors (brick architecture, in-memory API)
    // ═══════════════════════════════════════════════

    // ── NewLZDPCompressor ──
    py::class_<compressor::core_new::LZDPCompressor>(m, "NewLZDPCompressor")
        .def(py::init<>())
        .def(
            "compress",
            [](compressor::core_new::LZDPCompressor& self, py::buffer buf) {
                py::buffer_info info = buf.request();
                const auto* p = static_cast<const uint8_t*>(info.ptr);
                const size_t nbytes = static_cast<size_t>(info.shape[0]) * static_cast<size_t>(info.itemsize);
                std::vector<uint8_t> input(p, p + nbytes);
                auto result = compressor::algorithm::pipeline::compress_bytes(
                    input, self.getConfig().lzdp);
                return py::bytes(std::string(result.compressed.begin(), result.compressed.end()));
            },
            py::arg("data"))
        .def(
            "decompress",
            [](compressor::core_new::LZDPCompressor& self, py::buffer buf) {
                py::buffer_info info = buf.request();
                const auto* p = static_cast<const uint8_t*>(info.ptr);
                const size_t nbytes = static_cast<size_t>(info.shape[0]) * static_cast<size_t>(info.itemsize);
                std::vector<uint8_t> compressed(p, p + nbytes);
                auto dec = compressor::algorithm::pipeline::decompress_bytes(
                    compressed, self.getConfig().lzdp);
                return py::bytes(std::string(dec.begin(), dec.end()));
            },
            py::arg("data"))
        .def(
            "get_dp_visualization",
            [](compressor::core_new::LZDPCompressor& self, py::buffer buf) {
                py::buffer_info info = buf.request();
                const auto* p = static_cast<const uint8_t*>(info.ptr);
                const size_t nbytes = static_cast<size_t>(info.shape[0]) * static_cast<size_t>(info.itemsize);
                return self.get_dp_visualization(std::vector<uint8_t>(p, p + nbytes));
            },
            py::arg("data"))
        .def("get_algorithm_name",
             [](const compressor::core_new::LZDPCompressor&) { return std::string("LZDP (brick)"); })
        // Setter delegates to config_ (GUI compatibility, brick style)
        .def("set_search_size",
             [](compressor::core_new::LZDPCompressor& self, size_t v) {
                 self.config().lzdp.window.search_size = v; },
             py::arg("v"))
        .def("set_lookahead_size",
             [](compressor::core_new::LZDPCompressor& self, size_t v) {
                 self.config().lzdp.window.look_size = v; },
             py::arg("v"))
        .def("set_min_match",
             [](compressor::core_new::LZDPCompressor& self, size_t v) {
                 self.config().lzdp.window.min_match_len = v; },
             py::arg("v"))
        .def("set_dp_top",
             [](compressor::core_new::LZDPCompressor& self, size_t v) {
                 self.config().lzdp.dp.dp_top = static_cast<uint8_t>(v); },
             py::arg("v"))
        .def("set_match_engine",
             [](compressor::core_new::LZDPCompressor& self, int v) {
                 self.config().lzdp.dp.match_engine =
                     static_cast<compressor::algorithm::models::MatchEngine>(v); },
             py::arg("v"))
        .def("set_use_flag_encoding",
             [](compressor::core_new::LZDPCompressor& self, bool v) {
                 self.config().lzdp.encoding.use_flag_encoding = v; },
             py::arg("v"))
        .def("get_config",
             [](const compressor::core_new::LZDPCompressor& self) { return self.getConfig().lzdp; });

    // ── NewLZSSCompressor ──
    py::class_<compressor::core_new::LZSSCompressor>(m, "NewLZSSCompressor")
        .def(py::init<>())
        .def(
            "compress",
            [](compressor::core_new::LZSSCompressor& self, py::buffer buf) {
                py::buffer_info info = buf.request();
                const auto* p = static_cast<const uint8_t*>(info.ptr);
                const size_t nbytes = static_cast<size_t>(info.shape[0]) * static_cast<size_t>(info.itemsize);
                std::vector<uint8_t> input(p, p + nbytes);
                auto result = compressor::algorithm::pipeline::compress_bytes_lzss(
                    input, self.getConfig().lzss);
                return py::bytes(std::string(result.compressed.begin(), result.compressed.end()));
            },
            py::arg("data"))
        .def(
            "decompress",
            [](compressor::core_new::LZSSCompressor& self, py::buffer buf) {
                py::buffer_info info = buf.request();
                const auto* p = static_cast<const uint8_t*>(info.ptr);
                const size_t nbytes = static_cast<size_t>(info.shape[0]) * static_cast<size_t>(info.itemsize);
                std::vector<uint8_t> compressed(p, p + nbytes);
                auto dec = compressor::algorithm::pipeline::decompress_bytes_lzss(
                    compressed, self.getConfig().lzss);
                return py::bytes(std::string(dec.begin(), dec.end()));
            },
            py::arg("data"))
        .def("get_algorithm_name",
             [](const compressor::core_new::LZSSCompressor&) { return std::string("LZSS (brick)"); })
        .def("set_search_size",
             [](compressor::core_new::LZSSCompressor& self, size_t v) {
                 self.config().lzss.window.search_size = v; },
             py::arg("v"))
        .def("set_lookahead_size",
             [](compressor::core_new::LZSSCompressor& self, size_t v) {
                 self.config().lzss.window.look_size = v; },
             py::arg("v"))
        .def("set_min_match",
             [](compressor::core_new::LZSSCompressor& self, size_t v) {
                 self.config().lzss.window.min_match_len = v; },
             py::arg("v"));

    // ── NewDeflateCompressor ──
    py::class_<compressor::core_new::DeflateCompressor>(m, "NewDeflateCompressor")
        .def(py::init<>())
        .def(
            "compress",
            [](compressor::core_new::DeflateCompressor& self, py::buffer buf) {
                py::buffer_info info = buf.request();
                const auto* p = static_cast<const uint8_t*>(info.ptr);
                const size_t nbytes = static_cast<size_t>(info.shape[0]) * static_cast<size_t>(info.itemsize);
                std::vector<uint8_t> input(p, p + nbytes);
                auto result = compressor::algorithm::pipeline::compress_bytes_deflate(
                    input, self.getConfig().deflate);
                return py::bytes(std::string(result.compressed.begin(), result.compressed.end()));
            },
            py::arg("data"))
        .def(
            "decompress",
            [](compressor::core_new::DeflateCompressor& self, py::buffer buf) {
                py::buffer_info info = buf.request();
                const auto* p = static_cast<const uint8_t*>(info.ptr);
                const size_t nbytes = static_cast<size_t>(info.shape[0]) * static_cast<size_t>(info.itemsize);
                std::vector<uint8_t> compressed(p, p + nbytes);
                auto dec = compressor::algorithm::pipeline::decompress_bytes_deflate(
                    compressed, self.getConfig().deflate);
                return py::bytes(std::string(dec.begin(), dec.end()));
            },
            py::arg("data"))
        .def("get_algorithm_name",
             [](const compressor::core_new::DeflateCompressor&) { return std::string("Deflate (brick)"); })
        .def("set_search_size",
             [](compressor::core_new::DeflateCompressor& self, size_t v) {
                 self.config().deflate.window.search_size = v; },
             py::arg("v"))
        .def("set_lookahead_size",
             [](compressor::core_new::DeflateCompressor& self, size_t v) {
                 self.config().deflate.window.look_size = v; },
             py::arg("v"))
        .def("set_min_match",
             [](compressor::core_new::DeflateCompressor& self, size_t v) {
                 self.config().deflate.window.min_match_len = v; },
             py::arg("v"))
        .def("set_max_chain_length",
             [](compressor::core_new::DeflateCompressor&, size_t) {},
             py::arg("v"))
        .def("set_use_3hfmtree",
             [](compressor::core_new::DeflateCompressor& self, bool v) {
                 self.config().deflate.huffman.use_3hfmtree = v; },
             py::arg("v"))
        .def("set_use_flag_encoding",
             [](compressor::core_new::DeflateCompressor& self, bool v) {
                 self.config().deflate.encoding.use_flag_encoding = v; },
             py::arg("v"))
        .def("set_huffman_offset_chunk_bits",
             [](compressor::core_new::DeflateCompressor& self, size_t v) {
                 self.config().deflate.huffman.huffman_offset_bitwidth = static_cast<uint8_t>(v); },
             py::arg("v"))
        .def("set_huffman_length_chunk_bits",
             [](compressor::core_new::DeflateCompressor& self, size_t v) {
                 self.config().deflate.huffman.huffman_length_bitwidth = static_cast<uint8_t>(v); },
             py::arg("v"));

    // ── NewDPFlateCompressor ──
    py::class_<compressor::core_new::DPflateCompressor>(m, "NewDPFlateCompressor")
        .def(py::init<>())
        .def(
            "compress",
            [](compressor::core_new::DPflateCompressor& self, py::buffer buf) {
                py::buffer_info info = buf.request();
                const auto* p = static_cast<const uint8_t*>(info.ptr);
                const size_t nbytes = static_cast<size_t>(info.shape[0]) * static_cast<size_t>(info.itemsize);
                std::vector<uint8_t> input(p, p + nbytes);
                auto result = compressor::algorithm::pipeline::compress_bytes_dpflate(
                    input, self.getConfig().dpflate);
                return py::bytes(std::string(result.compressed.begin(), result.compressed.end()));
            },
            py::arg("data"))
        .def(
            "decompress",
            [](compressor::core_new::DPflateCompressor& self, py::buffer buf) {
                py::buffer_info info = buf.request();
                const auto* p = static_cast<const uint8_t*>(info.ptr);
                const size_t nbytes = static_cast<size_t>(info.shape[0]) * static_cast<size_t>(info.itemsize);
                std::vector<uint8_t> compressed(p, p + nbytes);
                auto dec = compressor::algorithm::pipeline::decompress_bytes_dpflate(
                    compressed, self.getConfig().dpflate);
                return py::bytes(std::string(dec.begin(), dec.end()));
            },
            py::arg("data"))
        .def("get_algorithm_name",
             [](const compressor::core_new::DPflateCompressor&) { return std::string("DPFlate (brick)"); })
        .def("set_search_size",
             [](compressor::core_new::DPflateCompressor& self, size_t v) {
                 self.config().dpflate.window.search_size = v; },
             py::arg("v"))
        .def("set_lookahead_size",
             [](compressor::core_new::DPflateCompressor& self, size_t v) {
                 self.config().dpflate.window.look_size = v; },
             py::arg("v"))
        .def("set_min_match",
             [](compressor::core_new::DPflateCompressor& self, size_t v) {
                 self.config().dpflate.min_match_len = v; },
             py::arg("v"))
        .def("set_dp_top",
             [](compressor::core_new::DPflateCompressor& self, size_t v) {
                 self.config().dpflate.dp.dp_top = static_cast<uint8_t>(v); },
             py::arg("v"))
        .def("set_match_engine",
             [](compressor::core_new::DPflateCompressor& self, int v) {
                 self.config().dpflate.dp.match_engine =
                     static_cast<compressor::algorithm::models::MatchEngine>(v); },
             py::arg("v"))
        .def("set_use_flag_encoding",
             [](compressor::core_new::DPflateCompressor& self, bool v) {
                 self.config().dpflate.encoding.use_flag_encoding = v; },
             py::arg("v"))
        .def("set_use_3hfmtree",
             [](compressor::core_new::DPflateCompressor& self, bool v) {
                 self.config().dpflate.huffman.use_3hfmtree = v; },
             py::arg("v"))
        .def("get_min_match",
             [](const compressor::core_new::DPflateCompressor& self) {
                 return self.getConfig().dpflate.min_match_len; })
        .def("get_match_engine",
             [](const compressor::core_new::DPflateCompressor& self) {
                 return static_cast<int>(self.getConfig().dpflate.dp.match_engine); })
        .def("get_config",
             [](const compressor::core_new::DPflateCompressor& self) { return self.getConfig().dpflate; });

    // ── Streaming Cancel (shared flag for all new streaming pipelines) ──
    m.def("set_new_streaming_cancel_requested",
          [](bool requested) {
              compressor::core_new::set_streaming_cancel_requested(requested);
          },
          py::arg("requested"),
          "Set cancel flag for algorithm_new streaming pipelines (LZSS/LZDP/Deflate/DPFlate).");
}

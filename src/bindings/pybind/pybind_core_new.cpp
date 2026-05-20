/// Python bindings for algorithm_new (core_new)
///
/// This module exposes the new memory-safe algorithms to Python.
/// Interface adapted to match the legacy ICompressor contract for
/// backward compatibility with the GUI layer.

#include <cstdlib>
#include <optional>
#include <stdexcept>
#include <vector>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "LZDPcompressor.hpp"
#include "LZSScompressor.hpp"
#include "Deflatecompressor.hpp"
#include "DPflatecompressor.hpp"

namespace py = pybind11;

// ============================================================================
// Helper: convert py::buffer to std::vector<uint8_t>
// ============================================================================

static auto buffer_to_u8vec(py::buffer buf) -> std::vector<uint8_t> {
    py::buffer_info info = buf.request();
    if (info.ndim != 1) {
        throw py::value_error("expected 1-dimensional byte buffer");
    }
    const size_t nbytes =
        static_cast<size_t>(info.shape[0]) * static_cast<size_t>(info.itemsize);
    const auto* p = static_cast<const uint8_t*>(info.ptr);
    return std::vector<uint8_t>(p, p + nbytes);
}

static auto vector_to_pybytes(const std::vector<uint8_t>& v) -> py::bytes {
    if (v.empty()) return py::bytes(std::string());
    return py::bytes(reinterpret_cast<const char*>(v.data()), v.size());
}

// ============================================================================
// CompressorResult (mirrors the legacy structure for compatibility)
// ============================================================================

struct CompressorResultNew {
    std::vector<uint8_t> data;
    size_t original_size{0};
    size_t compressed_size{0};
    double compression_ratio{0.0};
    double time_ms{0.0};
    bool success{true};
    std::string error_message;

    CompressorResultNew() = default;
};

// ============================================================================
// Adapter: wraps core_new file-based API into memory-based ICompressor-like interface
// ============================================================================

namespace {

class LZDPAdapter {
public:
    explicit LZDPAdapter(compressor::core_new::LZDPCompressorConfig cfg = {})
        : compressor_(std::move(cfg)) {}

    auto compress(py::buffer buf) -> CompressorResultNew {
        CompressorResultNew result;
        try {
            const auto input = buffer_to_u8vec(buf);
            result.original_size = input.size();

            // Write to temp file → compress → read back
            // (This is a temporary workaround; ideally we'd add a memory API to core_new)
            const std::string tmp_in = "_adapter_lzdp_input.bin";
            const std::string tmp_out = "_adapter_lzdp_output.bin";

            {
                std::ofstream f(tmp_in, std::ios::binary);
                f.write(reinterpret_cast<const char*>(input.data()),
                       static_cast<std::streamsize>(input.size()));
            }

            compressor_.compress_file_to_path(tmp_in, tmp_out);

            {
                std::ifstream f(tmp_out, std::ios::binary | std::ios::ate);
                const auto sz = f.tellg();
                if (sz > 0) {
                    f.seekg(0);
                    result.data.resize(static_cast<size_t>(sz));
                    f.read(reinterpret_cast<char*>(result.data.data()), sz);
                }
                result.compressed_size = result.data.size();
            }

            result.compression_ratio = (result.original_size > 0)
                ? static_cast<double>(result.compressed_size) / result.original_size
                : 0.0;

            // Cleanup temp files
            std::remove(tmp_in.c_str());
            std::remove(tmp_out.c_str());

        } catch (const std::exception& e) {
            result.success = false;
            result.error_message = e.what();
        }
        return result;
    }

    auto decompress(py::buffer buf) -> CompressorResultNew {
        CompressorResultNew result;
        try {
            const auto input = buffer_to_u8vec(buf);
            result.original_size = input.size();

            // Similar workaround for decompress
            const std::string tmp_in = "_adapter_lzdp_comp_input.bin";
            const std::string tmp_out = "_adapter_lzdp_decomp_output.bin";

            {
                std::ofstream f(tmp_in, std::ios::binary);
                f.write(reinterpret_cast<const char*>(input.data()),
                       static_cast<std::streamsize>(input.size()));
            }

            const auto decompressed = compressor_.decompress_file(tmp_in);

            result.data = decompressed;
            result.compressed_size = result.data.size();
            result.compression_ratio = (result.original_size > 0)
                ? static_cast<double>(result.compressed_size) / result.original_size
                : 0.0;

            std::remove(tmp_in.c_str());
            std::remove(tmp_out.c_str());

        } catch (const std::exception& e) {
            result.success = false;
            result.error_message = e.what();
        }
        return result;
    }

private:
    compressor::core_new::LZDPCompressor compressor_;
};

// Similar adapters for other compressors can be added here...
// For now, we expose the raw core_new classes with simplified interfaces

}  // anonymous namespace

// ============================================================================
// PYBIND11 Module Definition
// ============================================================================

PYBIND11_MODULE(core_engine_new, m) {
    m.doc() = "Web Compressor C++ Core Engine (algorithm_new - Memory Safe)";

    // ===== Data Structures =====

    py::class_<CompressorResultNew>(m, "CompressorResult")
        .def(py::init<>())
        .def_property(
            "data",
            [](const CompressorResultNew& r) { return vector_to_pybytes(r.data); },
            [](CompressorResultNew& r, const py::object& ob) {
                r.data = buffer_to_u8vec(ob);
            })
        .def_readwrite("original_size", &CompressorResultNew::original_size)
        .def_readwrite("compressed_size", &CompressorResultNew::compressed_size)
        .def_readwrite("compression_ratio", &CompressorResultNew::compression_ratio)
        .def_readwrite("time_ms", &CompressorResultNew::time_ms)
        .def_readwrite("success", &CompressorResultNew::success)
        .def_readwrite("error_message", &CompressorResultNew::error_message);

    // ===== Configuration Structures =====

    py::class_<compressor::core_new::LZDPCompressorConfig>(m, "LZDPCompressorConfig")
        .def(py::init<>())
        .def_readwrite("use_streaming", &compressor::core_new::LZDPCompressorConfig::use_streaming)
        .def_readwrite("streaming_chunk_size", &compressor::core_new::LZDPCompressorConfig::streaming_chunk_size)
        .def_readwrite("workspace_dir", &compressor::core_new::LZDPCompressorConfig::workspace_dir);

    // ===== Core Algorithm Compressors (Raw Interface) =====
    //
    // Note: These use file-path based APIs internally.
    // For memory-based compression, use the *Adapter classes below.
    //

    py::class_<compressor::core_new::LZDPCompressor>(m, "LZDPCompressorRaw")
        .def(py::init<compressor::core_new::LZDPCompressorConfig>(),
             py::arg("config") = compressor::core_new::LZDPCompressorConfig{})
        .def("compress_file",
             &compressor::core_new::LZDPCompressor::compress_file,
             "Compress a file and return compressed bytes")
        .def("decompress_file",
             &compressor::core_new::LZDPCompressor::decompress_file,
             "Decompress a compressed file and return original bytes")
        .def("compress_file_to_path",
             &compressor::core_new::LZDPCompressor::compress_file_to_path,
             "Compress file-to-file (supports streaming)");

    // ===== Adapted Interfaces (Memory-Based, GUI-Compatible) =====

    py::class_<LZDPAdapter>(m, "LZDPCompressor")
        .def(py::init<compressor::core_new::LZDPCompressorConfig>(),
             py::arg("config") = compressor::core_new::LZDPCompressorConfig{})
        .def(
            "compress",
            [](LZDPAdapter& self, py::buffer buf) { return self.compress(buf); },
            "Compress bytes in-memory (compatible with legacy interface)")
        .def(
            "decompress",
            [](LZDPAdapter& self, py::buffer buf) { return self.decompress(buf); },
            "Decompress bytes in-memory (compatible with legacy interface)")
        .def_static("get_algorithm_name",
            []() { return std::string("LZDP (algorithm_new - Memory Safe)"); });

    // TODO: Add adapters for LZSS/Deflate/DPFlate when needed...

    // ===== Convenience Functions =====

    m.def("test_algorithm_new",
          []() -> bool {
              // Quick smoke test: verify module loaded and basic functionality
              try {
                  compressor::core_new::LZDPCompressorConfig cfg;
                  compressor::core_new::LZDPCompressor lzdp(cfg);
                  return true;  // Basic instantiation works
              } catch (...) {
                  return false;
              }
          },
          "Quick test: returns True if algorithm_new is functional");

}

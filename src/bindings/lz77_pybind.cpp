#include <pybind11/pybind11.h>
#include "lz77_core.h"

namespace py = pybind11;
/*
这里负责处理python的接口，将python的接口转换为C++的接口，并调用C++的算法实现
*/

py::bytes py_compress(py::bytes input_bytes, swd search_size = 255, 
    lwd lookahead_size = 255) {
    std::string in_str = input_bytes;
    const uint8_t* in_buf = reinterpret_cast<const uint8_t*>(in_str.data());
    size_t in_len = in_str.size();
    
    if (in_len == 0) {
        return py::bytes("");
    }

    uint8_t* out_buf = nullptr;
    size_t out_len = 0;
    
    lz77Compress(in_buf, search_size, in_len, lookahead_size, &out_len, &out_buf);
    
    if (out_buf && out_len > 0) {
        std::string out_str(reinterpret_cast<const char*>(out_buf), out_len);
        delete[] out_buf;
        return py::bytes(out_str);
    }
    
    return py::bytes("");
}

py::bytes py_decompress(py::bytes compressed_bytes) {
    std::string comp_str = compressed_bytes;
    const uint8_t* comp_buf = reinterpret_cast<const uint8_t*>(comp_str.data());
    size_t comp_len = comp_str.size();
    
    if (comp_len == 0) {
        return py::bytes("");
    }

    // Heuristic for uncompressed size (e.g., 10x compressed size). In a real implementation, 
    // the original size should be stored in the header, or the decompressor should realloc.
    // For this test, we allocate a generously large buffer (up to 10MB).
    size_t max_out_size = comp_len * 100; 
    if (max_out_size < 1024 * 1024) max_out_size = 1024 * 1024 * 10;
    
    uint8_t* out_buf = new uint8_t[max_out_size];
    size_t out_len = 0;
    
    lz77Decompress(comp_buf, comp_len, out_buf, &out_len);
    
    if (out_len > 0 && out_len <= max_out_size) {
        std::string out_str(reinterpret_cast<const char*>(out_buf), out_len);
        delete[] out_buf;
        return py::bytes(out_str);
    }
    
    delete[] out_buf;
    return py::bytes("");
}

PYBIND11_MODULE(lz77, m) {
    m.doc() = "LZ77 Compression Python Bindings";
    
    m.def("compress", &py_compress, "Compress bytes using LZ77",
          py::arg("input_bytes"), py::arg("search_size") = 16, py::arg("lookahead_size") = 8);
          
    m.def("decompress", &py_decompress, "Decompress LZ77 compressed bytes",
          py::arg("compressed_bytes"));
}

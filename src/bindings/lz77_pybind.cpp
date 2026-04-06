#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "lz77.hpp"

namespace py = pybind11;

namespace {
    // 包装函数，将 std::vector<uint8_t> 转换为 py::bytes
    py::bytes compress_wrapper(core::algorithm::LZ77& lz, 
                               py::bytes input,
                               size_t search_size = 255,
                               size_t lookahead_size = 255,
                               core::algorithm::lz77MatchType match_type = core::algorithm::lz77MatchType::KMPNEXT) {
        std::string input_str = input.cast<std::string>();
        std::vector<uint8_t> input_vec(input_str.begin(), input_str.end());
        std::vector<uint8_t> result = lz.Compress(input_vec, search_size, lookahead_size, match_type);
        return py::bytes(reinterpret_cast<const char*>(result.data()), result.size());
    }
    
    py::bytes compress_ultra_wrapper(core::algorithm::LZ77& lz,
                                     py::bytes input,
                                     size_t search_size = 255,
                                     size_t lookahead_size = 255,
                                     size_t range = 3,
                                     core::algorithm::lz77MatchType match_type = core::algorithm::lz77MatchType::KMPNEXT) {
        std::string input_str = input.cast<std::string>();
        std::vector<uint8_t> input_vec(input_str.begin(), input_str.end());
        std::vector<uint8_t> result = lz.Compress_ultra(input_vec, search_size, lookahead_size, range, match_type);
        return py::bytes(reinterpret_cast<const char*>(result.data()), result.size());
    }
    
    py::bytes decompress_wrapper(core::algorithm::LZ77& lz,
                                 py::bytes input) {
        std::string input_str = input.cast<std::string>();
        std::vector<uint8_t> input_vec(input_str.begin(), input_str.end());
        std::vector<uint8_t> result = lz.Decompress(input_vec);
        return py::bytes(reinterpret_cast<const char*>(result.data()), result.size());
    }
}

PYBIND11_MODULE(lz77, m) {
    m.doc() = "LZ77 Compression Python Bindings - 直接绑定 C++ 类";
    
    // 先注册枚举类型
    py::enum_<core::algorithm::lz77MatchType>(m, "lz77MatchType")
        .value("KMPNEXT", core::algorithm::lz77MatchType::KMPNEXT, "KMP 匹配算法");
    
    // 绑定 LZ77 类
    py::class_<core::algorithm::LZ77>(m, "LZ77")
        .def(py::init<size_t, size_t>(), 
             py::arg("search_bytelength") = 2,
             py::arg("look_bytelength") = 2,
             "构造函数")
        .def("compress", 
             &compress_wrapper,
             py::arg("input"),
             py::arg("search_size") = 255,
             py::arg("lookahead_size") = 255,
             py::arg("match_type") = core::algorithm::lz77MatchType::KMPNEXT,
             "压缩数据（greedy 策略）")
        .def("compress_ultra", 
             &compress_ultra_wrapper,
             py::arg("input"),
             py::arg("search_size") = 255,
             py::arg("lookahead_size") = 255,
             py::arg("range") = 3,
             py::arg("match_type") = core::algorithm::lz77MatchType::KMPNEXT,
             "压缩数据（DP 多候选策略）")
        .def("decompress", 
             &decompress_wrapper,
             py::arg("input"),
             "解压数据");
}

#include <pybind11/pybind11.h>

namespace py = pybind11;

int g_counter = 0;

void add_to_counter(int delta) {
    g_counter += delta;
}

int get_counter() {
    return g_counter;
}

void reset_counter() {
    g_counter = 0;
}

PYBIND11_MODULE(c_api_test, m) {
    m.doc() = "pybind11 test module for counter updates";
    m.def("add_to_counter", &add_to_counter, py::arg("delta"));
    m.def("get_counter", &get_counter);
    m.def("reset_counter", &reset_counter);
}


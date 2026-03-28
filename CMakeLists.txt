cmake_minimum_required(VERSION 3.12)
project(lz77_pybind)

set(CMAKE_CXX_STANDARD 14)

# Find Python and Pybind11
find_package(Python3 COMPONENTS Interpreter Development REQUIRED)
find_package(pybind11 REQUIRED)

pybind11_add_module(lz77 
    lty_cores/lz77_core.cpp
    lty_cores/lz77_pybind.cpp
)

target_include_directories(lz77 PRIVATE lty_cores)

cmake_minimum_required(VERSION 3.16)
project(compression-tool LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_PYTHON "Build Pybind11 bindings for PyQt desktop app" ON)

add_subdirectory(src)

if(BUILD_PYTHON)
    add_subdirectory(src/bindings/pybind_new)
endif()
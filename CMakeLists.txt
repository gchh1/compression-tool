cmake_minimum_required(VERSION 3.24)

project(
    compression-tool
    VERSION 1.0
    LANGUAGES CXX
)

# ========= set c++ standard =========
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ========= set variables for paths ========= 
set(THIRD_PARTY ${CMAKE_SOURCE_DIR}/third_party)
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(TESTS_DIR ${CMAKE_SOURCE_DIR}/tests)
set(ALGORITHM_DIR ${SRC_DIR}/algorithm)
set(BINDINGS_DIR ${SRC_DIR}/bindings)
set(UTILS_DIR ${SRC_DIR}/utils)
set(MEMORY_DIR ${SRC_DIR}/memory)

# ========= set build option ==========
option(BUILD_WASM "Build WebAssembly bindings for browser frontend" OFF)
option(BUILD_PYTHON "Build Pybind11 bindings for PyQt desktop app" ON)

# ========= add subdirectories =========
add_subdirectory("${SRC_DIR}")
add_subdirectory("${TESTS_DIR}")

if(BUILD_WASM)
    add_subdirectory("${BINDINGS_DIR}/wasm")
endif()

if(BUILD_PYTHON)
    add_subdirectory("${BINDINGS_DIR}/pybind")
endif()
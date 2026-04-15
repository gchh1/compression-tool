cmake_minimum_required(VERSION 3.24)

project(
    compression-tool 
    VERSION 1.0
    LANGUAGES CXX
) 

# ========= set c++ standard =========
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ========= set variables for paths ========= 
set(THIRD_PARTY ${CMAKE_SOURCE_DIR}/third_party)
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(TESTS_DIR ${CMAKE_SOURCE_DIR}/tests)

set(BINDINGS_DIR ${SRC_DIR}/bindings)
set(CORE_DIR ${SRC_DIR}/core)
set(ALGORITHM_DIR ${SRC_DIR}/algorithm)

# ========= set build option ==========
option(BUILD_WASM "Build WebAssembly bindings for browser frontend" OFF)
option(BUILD_PYTHON "Build Pybind11 bindings for PyQt desktop app" OFF)

# ========= add subdirectories =========
add_subdirectory("${ALGORITHM_DIR}")
add_subdirectory("${CORE_DIR}")
add_subdirectory("${TESTS_DIR}")

if (BUILD_WASM)
    add_subdirectory("${BINDINGS_DIR}/wasm")
endif()

if (BUILD_PYTHON)
    add_subdirectory("${BINDINGS_DIR}/pybind")
endif()
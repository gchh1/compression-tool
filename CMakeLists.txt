cmake_minimum_required(VERSION 3.24)

project(
    compression-tool 
    VERSION 1.0
    LANGUAGES CXX) 

# ========= set c++ standard =========
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ========= set variables for paths ========= 
set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
set(TESTS_DIR ${CMAKE_SOURCE_DIR}/tests)

set(CORE_DIR ${SRC_DIR}/core)
set(ALGORITHM_DIR ${SRC_DIR}/algorithm)

# ========= add subdirectories =========
add_subdirectory("${CORE_DIR}")
add_subdirectory("${TESTS_DIR}")
add_subdirectory("${ALGORITHM_DIR}")

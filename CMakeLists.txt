cmake_minimum_required(VERSION 3.24)

project(
    compression-tool 
    VERSION 1.0
    LANGUAGES CXX) 

# ========= set c++ standard =========
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ========= set variables for paths ========= 
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)


# ========= add subdirectories =========
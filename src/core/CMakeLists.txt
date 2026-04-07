add_library(
    compress_core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/DeflateCompressor.cpp
)

target_include_directories(
    compress_core
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
    compress_core
    PRIVATE
    algorithm_lib
)
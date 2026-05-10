add_library(
    core
    STATIC
    AlgorithmFactory.cpp
    LZSSCompressor.cpp
    DeflateCompressor.cpp
    LZDPCompressor.cpp
    DPFlateCompressor.cpp
    GzipCompressor.cpp
    BrotliCompressor.cpp
    ZstdCompressor.cpp
)

target_include_directories(
    core
    PUBLIC
    include
)

target_link_libraries(
    core
    PUBLIC
    algorithm
    z
)
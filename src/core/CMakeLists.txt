add_library(
    core
    STATIC
    AlgorithmFactory.cpp
    LZSSCompressor.cpp
    DeflateCompressor.cpp
    LZMineCompressor.cpp
    LZCrazyCompressor.cpp
    CrazyFlateCompressor.cpp
    MyFlateCompressor.cpp
    GzipCompressor.cpp
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
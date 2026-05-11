add_library(
    api
    STATIC
    api.cpp
    WCXProtocol.cpp
    wcx_decompress_input.cpp
)

target_include_directories(
    api
    PUBLIC
    include
)

target_link_libraries(
    api
    PUBLIC
    archiver
    processor
    core
    utils
)

add_library(
    api
    STATIC
    CompressorAPI.cpp
)

target_include_directories(
    api
    PUBLIC
    include
)

target_link_libraries(
    api
    PUBLIC
    preprocessor
    core
)
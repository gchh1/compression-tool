add_library(
    processor
    STATIC
    StreamProcessor.cpp
)

target_include_directories(
    processor
    PUBLIC
    include
)

target_link_libraries(
    processor
    PUBLIC
    algorithm
    core
)
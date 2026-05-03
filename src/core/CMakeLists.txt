add_library(
    core
    STATIC
    AlgorithmFactory.cpp
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
)
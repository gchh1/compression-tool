add_library(
    core
    STATIC
    AlgorithmFactory.cpp
    ../processor/ChunkedStreamAdapter.cpp
)

target_include_directories(
    core
    PUBLIC
    include
    ../processor/include
    ${THIRD_PARTY}
)

target_link_libraries(
    core
    PUBLIC
    algorithm
)

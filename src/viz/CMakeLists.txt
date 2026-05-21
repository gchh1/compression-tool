add_library(
    viz
    STATIC
    BackgroundWriter.cpp
    DiskVizObserver.cpp
    EntropyCollector.cpp
)

target_include_directories(
    viz
    PUBLIC
    include
    ${UTILS_DIR}/include
)

target_link_libraries(
    viz
    PUBLIC
    utils
)

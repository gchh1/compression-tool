add_library(
    viz
    STATIC
    BackgroundWriter.cpp
    DiskVizObserver.cpp
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

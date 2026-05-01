add_library(
    api
    STATIC
    api.cpp
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
    memory
)

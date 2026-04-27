add_library(
    memory
    STATIC
    Chunk.cpp
    Memory_pool.cpp
)

target_include_directories(
    memory
    PUBLIC
    include
)
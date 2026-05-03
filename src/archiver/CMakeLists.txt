add_library(
    archiver
    STATIC
    FileReader.cpp
    PackReader.cpp
    PackWriter.cpp
)

target_include_directories(
    archiver
    PUBLIC
    include
)

target_link_libraries(
    archiver
    PUBLIC
    core
    processor
)
add_library(
    archiver
    STATIC
    FileReader.cpp
    PackReader.cpp
    PackWriter.cpp
    WCXProtocol.cpp
    WCXReader.cpp
)

target_include_directories(
    archiver
    PUBLIC
    include
    ${CMAKE_SOURCE_DIR}/src/api/include
)

target_link_libraries(
    archiver
    PUBLIC
    core
    processor
)
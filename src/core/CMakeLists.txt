add_library(
    compress_core
    ${CMAKE_CURRENT_SOURCE_DIR}/src/DeflateCompressor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/LZSSCompressor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Archiver.cpp
)

target_include_directories(
    compress_core
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${THIRD_PARTY}
)

target_link_libraries(
    compress_core
    PRIVATE
    algorithm_lib
)

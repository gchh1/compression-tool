add_library(
    preprocessor
    STATIC
    Archiver.cpp
    ImageParser.cpp
    ImagePreprocessor.cpp
)

target_include_directories(
    preprocessor
    PUBLIC
    include
    ${THIRD_PARTY}
)

target_link_libraries(
    preprocessor
    PUBLIC
    algorithm
)
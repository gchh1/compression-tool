# ADE (Algorithm Decision Engine) Module CMake Configuration
# Feature Vector Extraction Framework v3.0

cmake_minimum_required(VERSION 3.16)
project(ADE_Module VERSION 3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /O2)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O2)
endif()

set(ADE_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(
    ade
    STATIC
    features/FeatureExtractorV3.cpp
    features/FeatureVector.cpp
    features/MagicBytesDetector.cpp
    models/EvolutionaryAlgorithms.cpp
    models/ParamRegressorNet.cpp
    models/RandomForest.cpp
    runtime/ADEBridge.cpp
    runtime/DecisionEngine.cpp
)

target_include_directories(
    ade
    PUBLIC
        ${ADE_INCLUDE}
        ${CMAKE_SOURCE_DIR}/src
)

if(WIN32)
    target_link_libraries(ade PRIVATE)
endif()

add_executable(test_ade_feature_extractor
    ${CMAKE_SOURCE_DIR}/tests/cpp/ade/test_feature_extractor.cpp
)

target_link_libraries(test_ade_feature_extractor
    PRIVATE ade
)

add_executable(test_ade_decision_engine
    ${CMAKE_SOURCE_DIR}/tests/cpp/ade/test_decision_engine.cpp
)

target_link_libraries(test_ade_decision_engine
    PRIVATE ade
)

enable_testing()
add_test(NAME ADEFeatureExtractor COMMAND test_ade_feature_extractor)
add_test(NAME ADEDecisionEngine COMMAND test_ade_decision_engine)

add_executable(test_ade_evolutionary
    ${CMAKE_SOURCE_DIR}/tests/cpp/ade/test_evolutionary.cpp
)

target_link_libraries(test_ade_evolutionary
    PRIVATE ade
)

add_test(NAME ADEEvolutionary COMMAND test_ade_evolutionary)

option(ADE_BUILD_BENCHMARK "Build performance benchmark" ON)
if(ADE_BUILD_BENCHMARK)
    add_executable(bench_ade_extractor
        bench/benchmark_extractor.cpp
    )
    target_link_libraries(bench_ade_extractor PRIVATE ade)
endif()

option(ADE_BUILD_TOOLS "Build training and utility tools" ON)
if(ADE_BUILD_TOOLS)
    add_executable(train_ade_model
        tools/train_model.cpp
    )
    target_link_libraries(train_ade_model PRIVATE ade)

    add_executable(generate_ade_dataset
        tools/generate_dataset.cpp
    )
    target_link_libraries(generate_ade_dataset PRIVATE ade)
endif()

install(TARGETS ade
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

install(DIRECTORY include/ DESTINATION include/ade FILES_MATCHING PATTERN "*.hpp")

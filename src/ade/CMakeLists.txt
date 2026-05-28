add_library(ade STATIC
    runtime/ADEBridge.cpp
    runtime/DecisionEngine.cpp
    models/RandomForest.cpp
    models/EvolutionaryAlgorithms.cpp
    models/ParamRegressorNet.cpp
)

target_include_directories(ade PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/features
    ${CMAKE_CURRENT_SOURCE_DIR}/runtime
    ${CMAKE_CURRENT_SOURCE_DIR}/models
)

target_compile_features(ade PUBLIC cxx_std_20)
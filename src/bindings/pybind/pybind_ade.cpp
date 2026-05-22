#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include "ADEBridge.hpp"
#include "EvolutionaryAlgorithms.hpp"

namespace py = pybind11;

void init_ade(py::module_& m) {
    py::class_<compressor::ade::ADEBridgeResult>(m, "ADEResult")
        .def(py::init<>())
        .def_readwrite("algorithm", &compressor::ade::ADEBridgeResult::algorithm)
        .def_readwrite("estimated_ratio", &compressor::ade::ADEBridgeResult::estimated_ratio)
        .def_readwrite("confidence", &compressor::ade::ADEBridgeResult::confidence)
        .def_readwrite("reason", &compressor::ade::ADEBridgeResult::reason)
        .def_readwrite("file_type", &compressor::ade::ADEBridgeResult::file_type)
        .def_readwrite("shannon_entropy", &compressor::ade::ADEBridgeResult::shannon_entropy)
        .def_readwrite("extraction_time_ms", &compressor::ade::ADEBridgeResult::extraction_time_ms);

    py::class_<compressor::ade::ADEBridge>(m, "ADE")
        .def(py::init<>())
        .def("analyze", static_cast<compressor::ade::ADEBridgeResult(
                compressor::ade::ADEBridge::*)(const std::vector<uint8_t>&)>(
                    &compressor::ade::ADEBridge::analyze),
            py::arg("data"),
            py::call_guard<py::gil_scoped_release>(),
            "Analyze data and recommend compression algorithm")
        .def("analyze_file", &compressor::ade::ADEBridge::analyze_file,
            py::arg("filepath"),
            py::call_guard<py::gil_scoped_release>(),
            "Analyze a file and recommend compression algorithm")
        .def("set_mode", [](compressor::ade::ADEBridge& bridge, int mode) {
            auto m = static_cast<compressor::ade::AlgorithmDecisionEngine::Mode>(mode);
            bridge.set_mode(m);
        }, py::arg("mode"), "Set decision mode: 0=RuleBased, 1=MLHybrid, 2=MLOnly")
        .def("is_ml_ready", &compressor::ade::ADEBridge::is_ml_ready)
        .def("get_feature_importance", &compressor::ade::ADEBridge::get_feature_importance)
        .def("to_json", &compressor::ade::ADEBridge::to_json)
        .def("load_model", &compressor::ade::ADEBridge::load_model,
            py::arg("filepath"),
            "Load a trained RF model from binary file")
        .def("save_model", &compressor::ade::ADEBridge::save_model,
            py::arg("filepath"),
            "Save the current RF model to binary file")
        .def("try_load_default_model", &compressor::ade::ADEBridge::try_load_default_model,
            "Try to load default model from standard paths");
}

void init_ea(py::module_& m) {
    py::class_<compressor::ade::AlgorithmParams>(m, "AlgorithmParams")
        .def(py::init<>())
        .def_readwrite("window_size", &compressor::ade::AlgorithmParams::window_size)
        .def_readwrite("min_match", &compressor::ade::AlgorithmParams::min_match)
        .def_readwrite("max_chain_length", &compressor::ade::AlgorithmParams::max_chain_length)
        .def_readwrite("lookahead_size", &compressor::ade::AlgorithmParams::lookahead_size)
        .def_readwrite("dp_range", &compressor::ade::AlgorithmParams::dp_range)
        .def("to_string", &compressor::ade::AlgorithmParams::to_string);

    py::class_<compressor::ade::ParameterBounds>(m, "ParameterBounds")
        .def(py::init<>())
        .def_readwrite("window_size_min", &compressor::ade::ParameterBounds::window_size_min)
        .def_readwrite("window_size_max", &compressor::ade::ParameterBounds::window_size_max)
        .def_readwrite("min_match_min", &compressor::ade::ParameterBounds::min_match_min)
        .def_readwrite("min_match_max", &compressor::ade::ParameterBounds::min_match_max)
        .def_readwrite("max_chain_min", &compressor::ade::ParameterBounds::max_chain_min)
        .def_readwrite("max_chain_max", &compressor::ade::ParameterBounds::max_chain_max)
        .def_readwrite("lookahead_min", &compressor::ade::ParameterBounds::lookahead_min)
        .def_readwrite("lookahead_max", &compressor::ade::ParameterBounds::lookahead_max)
        .def_readwrite("dp_range_min", &compressor::ade::ParameterBounds::dp_range_min)
        .def_readwrite("dp_range_max", &compressor::ade::ParameterBounds::dp_range_max);

    py::class_<compressor::ade::OptimizationResult>(m, "OptimizationResult")
        .def(py::init<>())
        .def_readwrite("best_params", &compressor::ade::OptimizationResult::best_params)
        .def_readwrite("best_fitness", &compressor::ade::OptimizationResult::best_fitness)
        .def_readwrite("generations_completed", &compressor::ade::OptimizationResult::generations_completed)
        .def_readwrite("total_evaluations", &compressor::ade::OptimizationResult::total_evaluations)
        .def_readwrite("elapsed_ms", &compressor::ade::OptimizationResult::elapsed_ms)
        .def_readwrite("converged", &compressor::ade::OptimizationResult::converged)
        .def_readwrite("algorithm_name", &compressor::ade::OptimizationResult::algorithm_name);

    py::enum_<compressor::ade::ParameterOptimizer::Algorithm>(m, "EAAlgorithm")
        .value("NONE", compressor::ade::ParameterOptimizer::Algorithm::NONE)
        .value("GENETIC_ALGORITHM", compressor::ade::ParameterOptimizer::Algorithm::GENETIC_ALGORITHM)
        .value("PARTICLE_SWARM", compressor::ade::ParameterOptimizer::Algorithm::PARTICLE_SWARM)
        .value("CMA_ES", compressor::ade::ParameterOptimizer::Algorithm::CMA_ES)
        .export_values();

    py::class_<compressor::ade::ParameterOptimizer>(m, "ParameterOptimizer")
        .def(py::init<>())
        .def("set_algorithm", [](compressor::ade::ParameterOptimizer& opt, int algo) {
            opt.set_algorithm(static_cast<compressor::ade::ParameterOptimizer::Algorithm>(algo));
        }, py::arg("algorithm"), "Set EA algorithm: 0=None, 1=GA, 2=PSO, 3=CMA-ES")
        .def("get_algorithm", [](const compressor::ade::ParameterOptimizer& opt) -> int {
            return static_cast<int>(opt.get_algorithm());
        }, "Get current EA algorithm")
        .def("optimize", [](compressor::ade::ParameterOptimizer& opt,
                            std::function<double(compressor::ade::AlgorithmParams)> fitness,
                            const compressor::ade::ParameterBounds& bounds,
                            int algo_id, uint64_t max_time_ms) -> compressor::ade::OptimizationResult {
            return opt.optimize(fitness, bounds,
                static_cast<compressor::ade::AlgorithmID>(algo_id), max_time_ms);
        },
        py::arg("fitness"), py::arg("bounds"), py::arg("algo_id"),
        py::arg("max_time_ms") = 5000,
        py::call_guard<py::gil_scoped_release>(),
        "Run parameter optimization with given fitness function")
        .def("to_json", &compressor::ade::ParameterOptimizer::to_json);
}
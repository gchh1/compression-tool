#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include "ADEBridge.hpp"
#include "EvolutionaryAlgorithms.hpp"
#include "ParamRegressorNet.hpp"
#include "RandomForest.hpp"

namespace py = pybind11;

void init_ade(py::module_& m) {
    py::class_<compressor::ade::RandomForestConfig>(m, "RandomForestConfig")
        .def(py::init<>())
        .def_readwrite("num_trees", &compressor::ade::RandomForestConfig::num_trees)
        .def_readwrite("max_depth", &compressor::ade::RandomForestConfig::max_depth)
        .def_readwrite("min_samples_split",
                       &compressor::ade::RandomForestConfig::min_samples_split)
        .def_readwrite("min_samples_leaf",
                       &compressor::ade::RandomForestConfig::min_samples_leaf)
        .def_readwrite("max_features", &compressor::ade::RandomForestConfig::max_features)
        .def_readwrite("random_seed", &compressor::ade::RandomForestConfig::random_seed)
        .def_readwrite("bootstrap", &compressor::ade::RandomForestConfig::bootstrap)
        .def_readwrite("bootstrap_ratio",
                       &compressor::ade::RandomForestConfig::bootstrap_ratio);

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
            "Try to load default model from standard paths")
        .def(
            "train",
            [](compressor::ade::ADEBridge& bridge,
               const std::vector<std::tuple<std::vector<float>, int, float>>& rows,
               const compressor::ade::RandomForestConfig& config) {
                std::vector<compressor::ade::TrainingSample> samples;
                samples.reserve(rows.size());
                for (const auto& row : rows) {
                    compressor::ade::TrainingSample sample;
                    sample.features = std::get<0>(row);
                    sample.label = std::get<1>(row);
                    sample.weight = std::get<2>(row);
                    samples.push_back(std::move(sample));
                }
                bridge.train(samples, config);
            },
            py::arg("rows"),
            py::arg("config") = compressor::ade::RandomForestConfig{},
            py::call_guard<py::gil_scoped_release>(),
            "Train RF classifier from (features, label, weight) rows")
        .def("predict_padded", &compressor::ade::ADEBridge::predict_padded,
            py::arg("features"),
            "Predict algorithm label from 33-dim padded features");
}

void init_param_regressor(py::module_& m) {
    py::class_<compressor::ade::ParamRegressorTrainConfig>(m, "ParamRegressorTrainConfig")
        .def(py::init<>())
        .def_readwrite("epochs", &compressor::ade::ParamRegressorTrainConfig::epochs)
        .def_readwrite("learning_rate", &compressor::ade::ParamRegressorTrainConfig::learning_rate)
        .def_readwrite("batch_size", &compressor::ade::ParamRegressorTrainConfig::batch_size)
        .def_readwrite("validation_split",
                       &compressor::ade::ParamRegressorTrainConfig::validation_split)
        .def_readwrite("random_seed", &compressor::ade::ParamRegressorTrainConfig::random_seed);

    py::class_<compressor::ade::ParamRegressorTrainMetrics>(m, "ParamRegressorTrainMetrics")
        .def(py::init<>())
        .def_readonly("final_train_loss",
                      &compressor::ade::ParamRegressorTrainMetrics::final_train_loss)
        .def_readonly("final_val_loss",
                      &compressor::ade::ParamRegressorTrainMetrics::final_val_loss)
        .def_readonly("epochs_completed",
                      &compressor::ade::ParamRegressorTrainMetrics::epochs_completed)
        .def_readonly("num_samples", &compressor::ade::ParamRegressorTrainMetrics::num_samples);

    py::class_<compressor::ade::ParamRegressorNet>(m, "ParamRegressorNet")
        .def(py::init<>())
        .def("is_trained", &compressor::ade::ParamRegressorNet::is_trained)
        .def("predict", &compressor::ade::ParamRegressorNet::predict, py::arg("features"),
             py::call_guard<py::gil_scoped_release>())
        .def(
            "train",
            [](compressor::ade::ParamRegressorNet& net,
               const std::vector<std::tuple<std::vector<float>, std::vector<float>, float>>&
                   rows,
               const compressor::ade::ParamRegressorTrainConfig& config) {
                std::vector<compressor::ade::ParamRegressionRow> data;
                data.reserve(rows.size());
                for (const auto& row : rows) {
                    compressor::ade::ParamRegressionRow r;
                    r.features = std::get<0>(row);
                    r.targets = std::get<1>(row);
                    r.weight = std::get<2>(row);
                    data.push_back(std::move(r));
                }
                return net.train(data, config);
            },
            py::arg("rows"), py::arg("config") = compressor::ade::ParamRegressorTrainConfig{},
            py::call_guard<py::gil_scoped_release>())
        .def("save", &compressor::ade::ParamRegressorNet::save, py::arg("filepath"))
        .def("load", &compressor::ade::ParamRegressorNet::load, py::arg("filepath"))
        .def("try_load_default", &compressor::ade::ParamRegressorNet::try_load_default);
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
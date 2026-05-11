#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <vector>

#include "EvolutionaryAlgorithms.hpp"
#include "DecisionEngine.hpp"

using namespace compressor::ade;

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    static void test_##name(); \
    struct TestRunner_##name { \
        TestRunner_##name() { test_##name(); } \
    } runner_##name; \
    static void test_##name()

#define ASSERT_TRUE(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL [" << __func__ << "] " << msg << " (line " << __LINE__ << ")\n"; \
            tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_NEAR(a, b, eps, msg) \
    do { \
        if (std::abs((a) - (b)) > (eps)) { \
            std::cerr << "FAIL [" << __func__ << "] " << msg << ": got " << (a) << ", expected ~" << (b) << "\n"; \
            tests_failed++; \
            return; \
        } \
    } while(0)

#define PASS(msg) \
    do { \
        std::cout << "PASS: " << msg << "\n"; \
        tests_passed++; \
    } while(0)

static double evaluation_count = 0;
static AlgorithmParams last_best_params;

auto mock_fitness(const AlgorithmParams& params) -> double {
    evaluation_count++;
    double ws = params.window_size / 32768.0;
    double mm = params.min_match / 3.0;
    double mc = params.max_chain_length / 128.0;
    double la = params.lookahead_size / 128.0;
    double dp = params.dp_range / 3.0;

    double target_ws = 2.0, target_mm = 1.5, target_mc = 4.0, target_la = 2.0, target_dp = 2.0;
    return std::pow(ws - target_ws, 2) + std::pow(mm - target_mm, 2) +
           std::pow(mc - target_mc, 2) + std::pow(la - target_la, 2) +
           std::pow(dp - target_dp, 2);
}

TEST(parameter_bounds_clamp) {
    ParameterBounds bounds;
    AlgorithmParams p;
    p.window_size = 999999;
    p.min_match = 99;
    p.max_chain_length = 9999;
    p.lookahead_size = 9999;
    p.dp_range = 99;
    bounds.clamp(p);
    ASSERT_TRUE(p.window_size <= bounds.window_size_max, "window_size clamped");
    ASSERT_TRUE(p.min_match <= bounds.min_match_max, "min_match clamped");
    ASSERT_TRUE(p.max_chain_length <= bounds.max_chain_max, "max_chain clamped");
    ASSERT_TRUE(p.lookahead_size <= bounds.lookahead_max, "lookahead clamped");
    ASSERT_TRUE(p.dp_range <= bounds.dp_range_max, "dp_range clamped");
    PASS("ParameterBounds clamp works correctly");
}

TEST(parameter_bounds_roundtrip) {
    ParameterBounds bounds;
    std::mt19937 rng(42);
    auto original = bounds.random_params(rng);
    auto vec = bounds.to_vector(original);
    auto restored = bounds.from_vector(vec);

    ASSERT_TRUE(original.window_size == restored.window_size,
                "window_size roundtrip");
    ASSERT_TRUE(original.min_match == restored.min_match,
                "min_match roundtrip");
    ASSERT_TRUE(original.max_chain_length == restored.max_chain_length,
                "max_chain roundtrip");
    ASSERT_TRUE(original.lookahead_size == restored.lookahead_size,
                "lookahead roundtrip");
    ASSERT_TRUE(original.dp_range == restored.dp_range,
                "dp_range roundtrip");
    PASS("ParameterBounds vector conversion roundtrip works");
}

TEST(ga_basic_optimization) {
    evaluation_count = 0;
    GeneticAlgorithm ga;
    GAConfig config;
    config.population_size = 20;
    config.max_generations = 30;
    config.mutation_rate = 0.2;
    config.crossover_rate = 0.8;
    config.random_seed = 42;
    config.max_time_ms = 10000;

    ParameterBounds bounds;
    auto result = ga.optimize(mock_fitness, bounds, config);

    ASSERT_TRUE(result.best_fitness < 10.0,
                "GA should find a good solution, got fitness=" +
                std::to_string(result.best_fitness));
    ASSERT_TRUE(result.generations_completed > 0,
                "GA should complete at least one generation");
    ASSERT_TRUE(result.total_evaluations > 0,
                "GA should perform evaluations");
    ASSERT_TRUE(result.algorithm_name == "GeneticAlgorithm",
                "Algorithm name should be GeneticAlgorithm");

    printf("  GA: best=%.4f, gens=%zu, evals=%zu, converged=%s\n",
           result.best_fitness, result.generations_completed,
           result.total_evaluations, result.converged ? "yes" : "no");
    printf("  Best params: %s\n", result.best_params.to_string().c_str());
    PASS("GeneticAlgorithm basic optimization works");
}

TEST(pso_basic_optimization) {
    evaluation_count = 0;
    ParticleSwarmOptimization pso;
    PSOConfig config;
    config.swarm_size = 20;
    config.max_iterations = 30;
    config.random_seed = 42;
    config.max_time_ms = 10000;

    ParameterBounds bounds;
    auto result = pso.optimize(mock_fitness, bounds, config);

    ASSERT_TRUE(result.best_fitness < 10.0,
                "PSO should find a good solution, got fitness=" +
                std::to_string(result.best_fitness));
    ASSERT_TRUE(result.total_evaluations > 0,
                "PSO should perform evaluations");
    ASSERT_TRUE(result.algorithm_name == "ParticleSwarmOptimization",
                "Algorithm name should be ParticleSwarmOptimization");

    printf("  PSO: best=%.4f, iters=%zu, evals=%zu, converged=%s\n",
           result.best_fitness, result.generations_completed,
           result.total_evaluations, result.converged ? "yes" : "no");
    printf("  Best params: %s\n", result.best_params.to_string().c_str());
    PASS("ParticleSwarmOptimization basic optimization works");
}

TEST(cmaes_basic_optimization) {
    evaluation_count = 0;
    CMAESOptimizer cmaes;
    CMAESConfig config;
    config.lambda = 16;
    config.max_iterations = 50;
    config.initial_step_size = 1.0;
    config.random_seed = 42;
    config.max_time_ms = 10000;

    ParameterBounds bounds;
    auto result = cmaes.optimize(mock_fitness, bounds, config);

    ASSERT_TRUE(result.best_fitness < 10.0,
                "CMA-ES should find a good solution, got fitness=" +
                std::to_string(result.best_fitness));
    ASSERT_TRUE(result.total_evaluations > 0,
                "CMA-ES should perform evaluations");
    ASSERT_TRUE(result.algorithm_name == "CMAES",
                "Algorithm name should be CMA-ES");

    printf("  CMA-ES: best=%.4f, iters=%zu, evals=%zu, converged=%s\n",
           result.best_fitness, result.generations_completed,
           result.total_evaluations, result.converged ? "yes" : "no");
    printf("  Best params: %s\n", result.best_params.to_string().c_str());
    PASS("CMA-ES basic optimization works");
}

TEST(optimizer_unified_interface) {
    evaluation_count = 0;
    ParameterOptimizer optimizer;

    optimizer.set_algorithm(ParameterOptimizer::Algorithm::GENETIC_ALGORITHM);
    ASSERT_TRUE(optimizer.get_algorithm() == ParameterOptimizer::Algorithm::GENETIC_ALGORITHM,
                "Algorithm should be set to GA");

    ParameterBounds bounds;
    auto result = optimizer.optimize(mock_fitness, bounds, AlgorithmID::LZDP, 5000);

    ASSERT_TRUE(result.best_fitness < 15.0,
                "Unified interface should work via GA, fitness=" +
                std::to_string(result.best_fitness));

    optimizer.set_algorithm(ParameterOptimizer::Algorithm::PARTICLE_SWARM);
    result = optimizer.optimize(mock_fitness, bounds, AlgorithmID::DEFLATE, 5000);
    ASSERT_TRUE(result.best_fitness < 15.0,
                "Unified interface should work via PSO, fitness=" +
                std::to_string(result.best_fitness));

    optimizer.set_algorithm(ParameterOptimizer::Algorithm::CMA_ES);
    result = optimizer.optimize(mock_fitness, bounds, AlgorithmID::BROTLI, 5000);
    ASSERT_TRUE(result.best_fitness < 15.0,
                "Unified interface should work via CMA-ES, fitness=" +
                std::to_string(result.best_fitness));

    auto json = optimizer.to_json();
    ASSERT_TRUE(!json.empty(), "JSON output should not be empty");

    PASS("ParameterOptimizer unified interface works for all algorithms");
}

TEST(convergence_detection) {
    evaluation_count = 0;
    GeneticAlgorithm ga;
    GAConfig config;
    config.population_size = 10;
    config.max_generations = 200;
    config.stagnation_limit = 5;
    config.convergence_threshold = 0.001;
    config.random_seed = 12345;
    config.mutation_rate = 0.05;

    ParameterBounds bounds;
    auto result = ga.optimize(mock_fitness, bounds, config);

    ASSERT_TRUE(result.converged || result.generations_completed >= config.max_generations,
                "Should either converge or hit max generations");
    PASS("Convergence detection mechanism works");
}

TEST(time_limit_respected) {
    evaluation_count = 0;
    GeneticAlgorithm ga;
    GAConfig config;
    config.population_size = 50;
    config.max_generations = 10000;
    config.max_time_ms = 50;
    config.random_seed = 42;

    ParameterBounds bounds;
    auto start = std::chrono::high_resolution_clock::now();
    auto result = ga.optimize(mock_fitness, bounds, config);
    auto end = std::chrono::high_resolution_clock::now();

    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        end - start).count();

    ASSERT_TRUE(elapsed_ms < config.max_time_ms + 200,
                "Should respect time limit (~" + std::to_string(elapsed_ms) +
                "ms vs limit " + std::to_string(config.max_time_ms) + "ms)");
    PASS("Time limit is respected by optimizer");
}

int main() {
    std::cout << "========================================\n";
    std::cout << "  ADE Evolutionary Algorithms Tests\n";
    std::cout << "========================================\n\n";

    test_parameter_bounds_clamp();
    test_parameter_bounds_roundtrip();
    test_ga_basic_optimization();
    test_pso_basic_optimization();
    test_cmaes_basic_optimization();
    test_optimizer_unified_interface();
    test_convergence_detection();
    test_time_limit_respected();

    std::cout << "\n========================================\n";
    std::cout << "  Results: " << tests_passed << " passed, "
              << tests_failed << " failed\n";
    std::cout << "========================================\n";

    return tests_failed > 0 ? 1 : 0;
}

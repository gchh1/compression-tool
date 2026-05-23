#pragma once

#define _USE_MATH_DEFINES
#include <cstdint>
#include <iosfwd>
#include <functional>
#include <limits>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "DecisionEngine.hpp"

namespace compressor {
namespace ade {

struct ParameterBounds {
    size_t window_size_min{1024};
    size_t window_size_max{131072};
    size_t min_match_min{2};
    size_t min_match_max{8};
    size_t max_chain_min{4};
    size_t max_chain_max{1024};
    size_t lookahead_min{16};
    size_t lookahead_max{512};
    size_t dp_range_min{1};
    size_t dp_range_max{8};//有点小了，后面再调整

    auto clamp(AlgorithmParams& params) const -> void;
    auto random_params(std::mt19937& rng) const -> AlgorithmParams;
    auto to_vector(const AlgorithmParams& params) const -> std::vector<double>;
    auto from_vector(const std::vector<double>& v) const -> AlgorithmParams;
};

using FitnessFunction = std::function<double(const AlgorithmParams&)>;//适应度函数类型，用于评估个体的适应度值

struct OptimizationResult {
    AlgorithmParams best_params;
    double best_fitness{std::numeric_limits<double>::max()};// 最佳适应度值
    size_t generations_completed{0};// 完成的代数
    size_t total_evaluations{0};// 总评估次数
    double elapsed_ms{0.0};// 花费的时间，单位毫秒
    bool converged{false};// 是否收敛
    std::string algorithm_name;// 算法名称

    auto to_string() const -> std::ostringstream;
};

// 算法的配置结构体
struct GAConfig {
    size_t population_size{50};
    size_t max_generations{100};
    double mutation_rate{0.15};
    double crossover_rate{0.8};
    double elitism_ratio{0.1};
    size_t tournament_size{3};
    size_t random_seed{42};
    double convergence_threshold{1e-6};
    size_t stagnation_limit{20};
    uint64_t max_time_ms{5000};
};

struct PSOConfig {
    size_t swarm_size{40};
    size_t max_iterations{100};
    double cognitive_weight{2.0};
    double social_weight{2.0};
    double inertia_weight{0.729};
    double inertia_decay{0.995};
    double velocity_clamp{0.5};
    size_t random_seed{42};
    double convergence_threshold{1e-6};
    size_t stagnation_limit{20};
    uint64_t max_time_ms{5000};
};

struct CMAESConfig {
    size_t lambda{20};
    size_t max_iterations{200};
    double initial_step_size{0.5};
    double target_fitness{0.0};
    size_t random_seed{42};
    double convergence_threshold{1e-8};
    size_t stagnation_limit{30};
    uint64_t max_time_ms{10000};
};

class GeneticAlgorithm {
public:
    struct Individual {
        AlgorithmParams params;
        double fitness{std::numeric_limits<double>::max()};
        bool evaluated{false};
    };

    GeneticAlgorithm() = default;

    auto optimize(FitnessFunction fitness,
                  const ParameterBounds& bounds,
                  const GAConfig& config = {}) -> OptimizationResult;

private:
    std::mt19937 rng_;
    std::uniform_real_distribution<double> real_dist_{0.0, 1.0};
    size_t evaluation_count_{0};

    auto initialize_population(size_t size, const ParameterBounds& bounds)
        -> std::vector<Individual>;

    auto evaluate_population(std::vector<Individual>& population,
                             FitnessFunction fitness,
                             const ParameterBounds& bounds,
                             size_t start_from = 0) -> void;

    auto select_elites(const std::vector<Individual>& population,
                       size_t count) -> std::vector<Individual>;

    auto tournament_select(const std::vector<Individual>& population,
                           size_t tournament_size) -> const Individual&;

    auto crossover(const AlgorithmParams& p1, const AlgorithmParams& p2,
                   const ParameterBounds& bounds)
        -> std::pair<AlgorithmParams, AlgorithmParams>;

    auto mutate(AlgorithmParams& params, const ParameterBounds& bounds) -> void;
};

class ParticleSwarmOptimization {
public:
    struct Particle {
        AlgorithmParams position;
        AlgorithmParams personal_best;
        double personal_best_fitness{std::numeric_limits<double>::max()};
        std::vector<double> velocity;
        double fitness{std::numeric_limits<double>::max()};
    };

    ParticleSwarmOptimization() = default;

    auto optimize(FitnessFunction fitness,
                  const ParameterBounds& bounds,
                  const PSOConfig& config = {}) -> OptimizationResult;

private:
    std::mt19937 rng_;
    std::uniform_real_distribution<double> real_dist_{0.0, 1.0};
    size_t evaluation_count_{0};

    auto initialize_swarm(size_t size, const ParameterBounds& bounds,
                          size_t num_params) -> std::vector<Particle>;

    auto compute_velocity_bound(size_t dim, const ParameterBounds& bounds) -> double;
};

class CMAESOptimizer {
public:
    CMAESOptimizer() = default;

    auto optimize(FitnessFunction fitness,
                  const ParameterBounds& bounds,
                  const CMAESConfig& config = {}) -> OptimizationResult;

private:
    std::mt19937 rng_;
    std::normal_distribution<double> gaussian_{0.0, 1.0};
    size_t evaluation_count_{0};

    auto initialize_mean(const ParameterBounds& bounds) -> std::vector<double>;

    auto eigendecomposition(std::vector<std::vector<double>>& C,
                            std::vector<std::vector<double>>& B,
                            std::vector<double>& D, size_t N) -> void;

    auto rotate(std::vector<std::vector<double>>& C,
                std::vector<std::vector<double>>& B,
                size_t p, size_t q, double theta, size_t N) -> void;
};

class ParameterOptimizer {
public:
    enum class Algorithm : uint8_t {
        NONE = 0,
        GENETIC_ALGORITHM,
        PARTICLE_SWARM,
        CMA_ES
    };

    ParameterOptimizer();

    auto set_algorithm(Algorithm algo) -> void;

    auto get_algorithm() const -> Algorithm;

    auto optimize(FitnessFunction fitness,
                  const ParameterBounds& bounds,
                  AlgorithmID algo_id,
                  uint64_t max_time_ms = 5000) -> OptimizationResult;

    auto to_json() const -> std::string;

    static auto algorithm_to_string(Algorithm algo) -> std::string;

private:
    Algorithm algorithm_;
    GeneticAlgorithm ga_;
    ParticleSwarmOptimization pso_;
    CMAESOptimizer cmaes_;
};

}  // namespace ade
}  // namespace compressor

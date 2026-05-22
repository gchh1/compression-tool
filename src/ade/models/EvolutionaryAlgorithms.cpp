#include "EvolutionaryAlgorithms.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <sstream>
#include <utility>

namespace compressor {
namespace ade {

auto ParameterBounds::clamp(AlgorithmParams& params) const -> void {
    params.window_size = std::clamp(params.window_size, window_size_min, window_size_max);
    params.min_match = std::clamp(params.min_match, min_match_min, min_match_max);
    params.max_chain_length = std::clamp(params.max_chain_length, max_chain_min, max_chain_max);
    params.lookahead_size = std::clamp(params.lookahead_size, lookahead_min, lookahead_max);
    params.dp_range = std::clamp(params.dp_range, dp_range_min, dp_range_max);
}

auto ParameterBounds::random_params(std::mt19937& rng) const -> AlgorithmParams {
    AlgorithmParams p;
    std::uniform_int_distribution<size_t> ws(window_size_min, window_size_max);
    std::uniform_int_distribution<size_t> mm(min_match_min, min_match_max);
    std::uniform_int_distribution<size_t> mc(max_chain_min, max_chain_max);
    std::uniform_int_distribution<size_t> la(lookahead_min, lookahead_max);
    std::uniform_int_distribution<size_t> dp(dp_range_min, dp_range_max);
    p.window_size = ws(rng);
    p.min_match = mm(rng);
    p.max_chain_length = mc(rng);
    p.lookahead_size = la(rng);
    p.dp_range = dp(rng);
    return p;
}

auto ParameterBounds::to_vector(const AlgorithmParams& params) const -> std::vector<double> {
    return {static_cast<double>(params.window_size),
            static_cast<double>(params.min_match),
            static_cast<double>(params.max_chain_length),
            static_cast<double>(params.lookahead_size),
            static_cast<double>(params.dp_range)};
}

auto ParameterBounds::from_vector(const std::vector<double>& v) const -> AlgorithmParams {
    AlgorithmParams p;
    if (v.size() >= 5) {
        p.window_size = static_cast<size_t>(std::round(v[0]));
        p.min_match = static_cast<size_t>(std::round(v[1]));
        p.max_chain_length = static_cast<size_t>(std::round(v[2]));
        p.lookahead_size = static_cast<size_t>(std::round(v[3]));
        p.dp_range = static_cast<size_t>(std::round(v[4]));
    }
    clamp(p);
    return p;
}

auto OptimizationResult::to_string() const -> std::ostringstream {
    std::ostringstream s;
    s << "OptimizationResult {\n";
    s << "  algorithm: " << algorithm_name << "\n";
    s << "  best_fitness: " << best_fitness << "\n";
    s << "  best_params: " << best_params.to_string() << "\n";
    s << "  generations: " << generations_completed << "\n";
    s << "  evaluations: " << total_evaluations << "\n";
    s << "  converged: " << (converged ? "yes" : "no") << "\n";
    s << "  elapsed_ms: " << elapsed_ms << "\n";
    s << "}";
    return s;
}

auto GeneticAlgorithm::optimize(FitnessFunction fitness,
                                const ParameterBounds& bounds,
                                const GAConfig& config) -> OptimizationResult {
    OptimizationResult result;
    result.algorithm_name = "GeneticAlgorithm";
    auto start_time = std::chrono::high_resolution_clock::now();

    rng_.seed(config.random_seed);
    evaluation_count_ = 0;

    size_t elite_count =
        std::max(size_t(1), static_cast<size_t>(config.population_size * config.elitism_ratio));

    auto population = initialize_population(config.population_size, bounds);
    evaluate_population(population, fitness, bounds);

    size_t stagnation_counter = 0;
    double prev_best = population[0].fitness;

    for (size_t gen = 0; gen < config.max_generations; ++gen) {
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        if (static_cast<uint64_t>(elapsed) >= config.max_time_ms) {
            result.generations_completed = gen;
            break;
        }

        std::sort(population.begin(), population.end(),
                  [](const Individual& a, const Individual& b) { return a.fitness < b.fitness; });

        double current_best = population[0].fitness;
        if (std::abs(current_best - prev_best) < config.convergence_threshold) {
            stagnation_counter++;
            if (stagnation_counter >= config.stagnation_limit) {
                result.converged = true;
                result.generations_completed = gen + 1;
                break;
            }
        } else {
            stagnation_counter = 0;
        }
        prev_best = current_best;

        auto new_population = select_elites(population, elite_count);

        while (new_population.size() < config.population_size) {
            auto parent1 = tournament_select(population, config.tournament_size);
            auto parent2 = tournament_select(population, config.tournament_size);

            if (real_dist_(rng_) < config.crossover_rate) {
                auto [child1, child2] = crossover(parent1.params, parent2.params, bounds);
                new_population.push_back({child1});
                if (new_population.size() < config.population_size) {
                    new_population.push_back({child2});
                }
            } else {
                new_population.push_back({parent1.params});
                if (new_population.size() < config.population_size) {
                    new_population.push_back({parent2.params});
                }
            }
        }

        for (size_t i = elite_count; i < new_population.size(); ++i) {
            if (real_dist_(rng_) < config.mutation_rate) {
                mutate(new_population[i].params, bounds);
            }
        }

        evaluate_population(new_population, fitness, bounds, elite_count);
        population = std::move(new_population);
        result.generations_completed = gen + 1;
    }

    std::sort(population.begin(), population.end(),
              [](const Individual& a, const Individual& b) { return a.fitness < b.fitness; });

    auto end_time = std::chrono::high_resolution_clock::now();
    result.elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    result.best_params = population[0].params;
    result.best_fitness = population[0].fitness;
    result.total_evaluations = evaluation_count_;

    return result;
}

auto GeneticAlgorithm::initialize_population(size_t size, const ParameterBounds& bounds)
    -> std::vector<Individual> {
    std::vector<Individual> pop(size);
    for (auto& ind : pop) {
        ind.params = bounds.random_params(rng_);
    }
    return pop;
}

auto GeneticAlgorithm::evaluate_population(std::vector<Individual>& population,
                                           FitnessFunction fitness,
                                           const ParameterBounds& bounds,
                                           size_t start_from) -> void {
    for (size_t i = start_from; i < population.size(); ++i) {
        bounds.clamp(population[i].params);
        population[i].fitness = fitness(population[i].params);
        population[i].evaluated = true;
        evaluation_count_++;
    }
}

auto GeneticAlgorithm::select_elites(const std::vector<Individual>& population,
                                     size_t count) -> std::vector<Individual> {
    std::vector<Individual> elites;
    elites.reserve(count);
    for (size_t i = 0; i < count && i < population.size(); ++i) {
        elites.push_back(population[i]);
    }
    return elites;
}

auto GeneticAlgorithm::tournament_select(const std::vector<Individual>& population,
                                         size_t tournament_size) -> const Individual& {
    std::uniform_int_distribution<size_t> idx_dist(0, population.size() - 1);
    const Individual* best = &population[idx_dist(rng_)];
    for (size_t t = 1; t < tournament_size; ++t) {
        const auto* candidate = &population[idx_dist(rng_)];
        if (candidate->fitness < best->fitness) {
            best = candidate;
        }
    }
    return *best;
}

auto GeneticAlgorithm::crossover(const AlgorithmParams& p1,
                                 const AlgorithmParams& p2,
                                 const ParameterBounds& bounds)
    -> std::pair<AlgorithmParams, AlgorithmParams> {
    AlgorithmParams c1;
    AlgorithmParams c2;
    std::bernoulli_distribution coin(0.5);

    if (coin(rng_)) {
        c1.window_size = p1.window_size;
        c2.window_size = p2.window_size;
    } else {
        c1.window_size = p2.window_size;
        c2.window_size = p1.window_size;
    }

    if (coin(rng_)) {
        c1.min_match = p1.min_match;
        c2.min_match = p2.min_match;
    } else {
        c1.min_match = p2.min_match;
        c2.min_match = p1.min_match;
    }

    if (coin(rng_)) {
        c1.max_chain_length = p1.max_chain_length;
        c2.max_chain_length = p2.max_chain_length;
    } else {
        c1.max_chain_length = p2.max_chain_length;
        c2.max_chain_length = p1.max_chain_length;
    }

    if (coin(rng_)) {
        c1.lookahead_size = p1.lookahead_size;
        c2.lookahead_size = p2.lookahead_size;
    } else {
        c1.lookahead_size = p2.lookahead_size;
        c2.lookahead_size = p1.lookahead_size;
    }

    if (coin(rng_)) {
        c1.dp_range = p1.dp_range;
        c2.dp_range = p2.dp_range;
    } else {
        c1.dp_range = p2.dp_range;
        c2.dp_range = p1.dp_range;
    }

    bounds.clamp(c1);
    bounds.clamp(c2);
    return {c1, c2};
}

auto GeneticAlgorithm::mutate(AlgorithmParams& params, const ParameterBounds& bounds) -> void {
    std::uniform_int_distribution<int> param_choice(0, 4);
    int choice = param_choice(rng_);
    double mutation_strength = 0.3;

    switch (choice) {
        case 0: {
            auto range = bounds.window_size_max - bounds.window_size_min;
            auto delta = static_cast<int64_t>((real_dist_(rng_) - 0.5) * 2 * range * mutation_strength);
            long long new_val = static_cast<long long>(params.window_size) + delta;
            params.window_size = static_cast<size_t>(
                std::clamp(new_val, static_cast<long long>(bounds.window_size_min),
                           static_cast<long long>(bounds.window_size_max)));
            break;
        }
        case 1: {
            auto delta = static_cast<int>((real_dist_(rng_) - 0.5) * 6);
            int new_val = static_cast<int>(params.min_match) + delta;
            params.min_match = static_cast<size_t>(
                std::clamp(new_val, static_cast<int>(bounds.min_match_min),
                           static_cast<int>(bounds.min_match_max)));
            break;
        }
        case 2: {
            auto range = bounds.max_chain_max - bounds.max_chain_min;
            auto delta = static_cast<int64_t>((real_dist_(rng_) - 0.5) * 2 * range * mutation_strength);
            long long new_val = static_cast<long long>(params.max_chain_length) + delta;
            params.max_chain_length = static_cast<size_t>(
                std::clamp(new_val, static_cast<long long>(bounds.max_chain_min),
                           static_cast<long long>(bounds.max_chain_max)));
            break;
        }
        case 3: {
            auto delta = static_cast<int>((real_dist_(rng_) - 0.5) * 100);
            int new_val = static_cast<int>(params.lookahead_size) + delta;
            params.lookahead_size = static_cast<size_t>(
                std::clamp(new_val, static_cast<int>(bounds.lookahead_min),
                           static_cast<int>(bounds.lookahead_max)));
            break;
        }
        case 4: {
            auto delta = static_cast<int>((real_dist_(rng_) - 0.5) * 4);
            int new_val = static_cast<int>(params.dp_range) + delta;
            params.dp_range = static_cast<size_t>(
                std::clamp(new_val, static_cast<int>(bounds.dp_range_min),
                           static_cast<int>(bounds.dp_range_max)));
            break;
        }
    }
}

auto ParticleSwarmOptimization::optimize(FitnessFunction fitness,
                                         const ParameterBounds& bounds,
                                         const PSOConfig& config) -> OptimizationResult {
    OptimizationResult result;
    result.algorithm_name = "ParticleSwarmOptimization";

    auto start_time = std::chrono::high_resolution_clock::now();
    rng_.seed(config.random_seed);
    evaluation_count_ = 0;

    constexpr size_t NUM_PARAMS = 5;
    auto swarm = initialize_swarm(config.swarm_size, bounds, NUM_PARAMS);

    AlgorithmParams global_best_position;
    double global_best_fitness = std::numeric_limits<double>::max();

    for (auto& particle : swarm) {
        bounds.clamp(particle.position);
        particle.fitness = fitness(particle.position);
        evaluation_count_++;
        if (particle.fitness < particle.personal_best_fitness) {
            particle.personal_best = particle.position;
            particle.personal_best_fitness = particle.fitness;
        }
        if (particle.fitness < global_best_fitness) {
            global_best_fitness = particle.fitness;
            global_best_position = particle.position;
        }
    }

    size_t stagnation_counter = 0;
    double prev_global_best = global_best_fitness;

    for (size_t iter = 0; iter < config.max_iterations; ++iter) {
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        if (static_cast<uint64_t>(elapsed) >= config.max_time_ms) {
            result.generations_completed = iter;
            break;
        }

        double w = config.inertia_weight * std::pow(config.inertia_decay, static_cast<double>(iter));
        auto gb_vec = bounds.to_vector(global_best_position);

        for (auto& particle : swarm) {
            auto pb_vec = bounds.to_vector(particle.personal_best);
            auto pos_vec = bounds.to_vector(particle.position);

            for (size_t d = 0; d < NUM_PARAMS; ++d) {
                double r1 = real_dist_(rng_);
                double r2 = real_dist_(rng_);

                particle.velocity[d] = w * particle.velocity[d] +
                                       config.cognitive_weight * r1 * (pb_vec[d] - pos_vec[d]) +
                                       config.social_weight * r2 * (gb_vec[d] - pos_vec[d]);

                double vmax = compute_velocity_bound(d, bounds);
                particle.velocity[d] = std::clamp(particle.velocity[d], -vmax, vmax);
                pos_vec[d] += particle.velocity[d];
            }

            particle.position = bounds.from_vector(pos_vec);
            bounds.clamp(particle.position);

            particle.fitness = fitness(particle.position);
            evaluation_count_++;
            if (particle.fitness < particle.personal_best_fitness) {
                particle.personal_best = particle.position;
                particle.personal_best_fitness = particle.fitness;
            }

            if (particle.fitness < global_best_fitness) {
                global_best_fitness = particle.fitness;
                global_best_position = particle.position;
            }
        }

        if (std::abs(global_best_fitness - prev_global_best) < config.convergence_threshold) {
            stagnation_counter++;
            if (stagnation_counter >= config.stagnation_limit) {
                result.converged = true;
                result.generations_completed = iter + 1;
                break;
            }
        } else {
            stagnation_counter = 0;
        }
        prev_global_best = global_best_fitness;
        result.generations_completed = iter + 1;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    result.best_params = global_best_position;
    result.best_fitness = global_best_fitness;
    result.total_evaluations = evaluation_count_;
    return result;
}

auto ParticleSwarmOptimization::initialize_swarm(size_t size,
                                                 const ParameterBounds& bounds,
                                                 size_t num_params) -> std::vector<Particle> {
    std::vector<Particle> swarm(size);
    for (auto& p : swarm) {
        p.position = bounds.random_params(rng_);
        p.personal_best = p.position;
        p.velocity.resize(num_params, 0.0);
    }
    return swarm;
}

auto ParticleSwarmOptimization::compute_velocity_bound(size_t dim,
                                                       const ParameterBounds& bounds) -> double {
    switch (dim) {
        case 0:
            return (bounds.window_size_max - bounds.window_size_min) * 0.25;
        case 1:
            return (bounds.min_match_max - bounds.min_match_min) * 0.25;
        case 2:
            return (bounds.max_chain_max - bounds.max_chain_min) * 0.25;
        case 3:
            return (bounds.lookahead_max - bounds.lookahead_min) * 0.25;
        case 4:
            return (bounds.dp_range_max - bounds.dp_range_min) * 0.25;
        default:
            return 100.0;
    }
}

auto CMAESOptimizer::optimize(FitnessFunction fitness,
                              const ParameterBounds& bounds,
                              const CMAESConfig& config) -> OptimizationResult {
    OptimizationResult result;
    result.algorithm_name = "CMAES";

    auto start_time = std::chrono::high_resolution_clock::now();
    rng_.seed(config.random_seed);
    evaluation_count_ = 0;

    constexpr size_t N = 5;
    auto mean = initialize_mean(bounds);
    auto sigma = config.initial_step_size;

    std::vector<std::vector<double>> C(N, std::vector<double>(N, 0.0));
    for (size_t i = 0; i < N; ++i) C[i][i] = 1.0;

    std::vector<double> pc(N, 0.0);
    std::vector<double> ps(N, 0.0);

    size_t lambda = config.lambda;
    double mu_half = std::floor(lambda / 2.0);
    size_t mu = static_cast<size_t>(mu_half) * 2;

    std::vector<double> weights(mu);
    for (size_t i = 0; i < mu; ++i) {
        weights[i] = std::log(mu_half + 0.5) - std::log(i + 1.0);
    }
    double w_sum = 0.0;
    for (size_t i = 0; i < mu; ++i) {
        w_sum += std::abs(weights[i]);
    }
    for (auto& w : weights) w /= w_sum;

    double mueff = 0.0;
    for (size_t i = 0; i < mu; ++i) {
        if (weights[i] > 0) mueff += weights[i] * weights[i];
    }
    mueff = 1.0 / mueff;

    double cc = (4.0 + mueff / N) / (N + 4.0 + 2.0 * mueff / N);
    double cs = (mueff + 2.0) / (N + mueff + 5.0);
    double c1 = 2.0 / ((N + 1.3) * (N + 1.3) + mueff);
    double cmu =
        std::min(1.0 - c1, 2.0 * (mueff - 2.0 + 1.0 / mueff) / ((N + 2.0) * (N + 2.0) + mueff));
    double damps = 1.0 + 2.0 * std::max(0.0, std::sqrt((mueff - 1.0) / (N + 1.0)) - 1.0) + cs;
    double chiN = std::sqrt(static_cast<double>(N)) *
                  (1.0 - 1.0 / (4.0 * N) + 1.0 / (21.0 * N * N));

    size_t eigen_eval = 0;
    std::vector<double> D(N, 1.0);
    std::vector<double> inv_D(N, 1.0);
    std::vector<std::vector<double>> B(N, std::vector<double>(N, 0.0));
    for (size_t i = 0; i < N; ++i) B[i][i] = 1.0;

    double best_fitness = std::numeric_limits<double>::max();
    AlgorithmParams best_params;
    size_t stagnation_counter = 0;
    double prev_best = best_fitness;

    for (size_t iter = 0; iter < config.max_iterations; ++iter) {
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        if (static_cast<uint64_t>(elapsed) >= config.max_time_ms) {
            result.generations_completed = iter;
            break;
        }

        for (size_t d = 0; d < N; ++d) {
            if (D[d] == 0) D[d] = 1e-20;
            inv_D[d] = 1.0 / D[d];
        }

        std::vector<std::pair<double, std::vector<double>>> population(lambda);
        for (size_t k = 0; k < lambda; ++k) {
            std::vector<double> z(N);
            for (size_t d = 0; d < N; ++d) z[d] = gaussian_(rng_);

            std::vector<double> y(N, 0.0);
            for (size_t i = 0; i < N; ++i) {
                for (size_t j = 0; j < N; ++j) {
                    y[i] += B[i][j] * D[j] * z[j];
                }
            }

            std::vector<double> x(N);
            for (size_t d = 0; d < N; ++d) x[d] = mean[d] + sigma * y[d];
            population[k] = {0.0, x};
        }

        for (auto& [fit, x] : population) {
            auto params = bounds.from_vector(x);
            fit = fitness(params);
            evaluation_count_++;
            if (fit < best_fitness) {
                best_fitness = fit;
                best_params = params;
            }
        }

        std::sort(population.begin(), population.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        std::vector<double> old_mean = mean;
        mean.assign(N, 0.0);
        for (size_t i = 0; i < mu; ++i) {
            for (size_t d = 0; d < N; ++d) {
                mean[d] += weights[i] * population[i].second[d];
            }
        }

        if (best_fitness - config.target_fitness < config.convergence_threshold ||
            (iter > 0 && std::abs(best_fitness - prev_best) < config.convergence_threshold)) {
            stagnation_counter++;
            if (stagnation_counter >= config.stagnation_limit) {
                result.converged = true;
                result.generations_completed = iter + 1;
                break;
            }
        } else {
            stagnation_counter = 0;
        }
        prev_best = best_fitness;

        std::vector<double> diff(N);
        for (size_t d = 0; d < N; ++d) diff[d] = (mean[d] - old_mean[d]) / sigma;

        for (size_t d = 0; d < N; ++d)
            ps[d] = (1.0 - cs) * ps[d] + std::sqrt(cs * (2.0 - cs) * mueff) * diff[d];

        double ps_norm = 0.0;
        for (size_t d = 0; d < N; ++d) ps_norm += ps[d] * ps[d];
        ps_norm = std::sqrt(ps_norm);

        double hsig = 0.0;
        if (ps_norm / std::sqrt(1.0 - std::pow(1.0 - cs, 2.0 * (iter + 1))) / chiN <
            1.4 + 2.0 / ((double)(N + 1))) {
            hsig = 1.0;
        }

        for (size_t d = 0; d < N; ++d)
            pc[d] = (1.0 - cc) * pc[d] + hsig * std::sqrt(cc * (2.0 - cc) * mueff) * diff[d];

        auto artmp = pc;
        for (size_t i = 0; i < N; ++i)
            for (size_t j = 0; j < N; ++j)
                C[i][j] = (1.0 - c1 - cmu) * C[i][j] +
                          c1 * (pc[i] * pc[j] + (1.0 - hsig) * cc * (2.0 - cc) * artmp[i]) +
                          cmu * artmp[j];

        artmp.assign(N, 0.0);
        for (size_t i = 0; i < mu; ++i) {
            if (weights[i] < 0) continue;
            std::vector<double>& y = population[i].second;
            for (size_t d = 0; d < N; ++d) artmp[d] += weights[i] * y[d];
        }
        for (size_t i = 0; i < N; ++i)
            for (size_t j = 0; j < N; ++j) C[i][j] += cmu * artmp[i] * artmp[j];

        eigen_eval++;
        if (eigen_eval > lambda / (c1 + cmu) / N / 10.0) {
            eigen_eval = 0;
            eigendecomposition(C, B, D, N);
        }

        sigma *= std::exp((cs / damps) * (ps_norm / chiN - 1.0));
        sigma = std::clamp(sigma, 1e-12, 1e2);
        result.generations_completed = iter + 1;
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    result.elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    result.best_params = best_params;
    result.best_fitness = best_fitness;
    result.total_evaluations = evaluation_count_;
    return result;
}

auto CMAESOptimizer::initialize_mean(const ParameterBounds& bounds) -> std::vector<double> {
    AlgorithmParams mid;
    mid.window_size = (bounds.window_size_min + bounds.window_size_max) / 2;
    mid.min_match = (bounds.min_match_min + bounds.min_match_max) / 2;
    mid.max_chain_length = (bounds.max_chain_min + bounds.max_chain_max) / 2;
    mid.lookahead_size = (bounds.lookahead_min + bounds.lookahead_max) / 2;
    mid.dp_range = (bounds.dp_range_min + bounds.dp_range_max) / 2;
    return bounds.to_vector(mid);
}

auto CMAESOptimizer::eigendecomposition(std::vector<std::vector<double>>& C,
                                        std::vector<std::vector<double>>& B,
                                        std::vector<double>& D,
                                        size_t N) -> void {
    for (size_t iteration = 0; iteration < 50; ++iteration) {
        double off_diag = 0.0;
        for (size_t i = 0; i < N; ++i)
            for (size_t j = i + 1; j < N; ++j) off_diag += C[i][j] * C[i][j];

        if (off_diag < 1e-12) break;

        size_t p = 0;
        size_t q = 1;
        double max_off = std::abs(C[0][1]);
        for (size_t i = 0; i < N; ++i) {
            for (size_t j = i + 1; j < N; ++j) {
                if (std::abs(C[i][j]) > max_off) {
                    max_off = std::abs(C[i][j]);
                    p = i;
                    q = j;
                }
            }
        }

        if (C[p][p] == C[q][q]) {
            double angle = 3.14159265358979323846 / 4.0;
            if (C[p][q] > 0) angle = -angle;
            rotate(C, B, p, q, angle, N);
        } else {
            double phi = 0.5 * atan2(2.0 * C[p][q], C[q][q] - C[p][p]);
            rotate(C, B, p, q, phi, N);
        }
    }

    for (size_t i = 0; i < N; ++i) {
        D[i] = std::sqrt(std::max(0.0, C[i][i]));
    }
}

auto CMAESOptimizer::rotate(std::vector<std::vector<double>>& C,
                            std::vector<std::vector<double>>& B,
                            size_t p,
                            size_t q,
                            double theta,
                            size_t N) -> void {
    double c = std::cos(theta);
    double s = std::sin(theta);

    std::vector<double> Cp = C[p];
    std::vector<double> Cq = C[q];
    for (size_t i = 0; i < N; ++i) {
        C[p][i] = c * Cp[i] + s * Cq[i];
        C[q][i] = -s * Cp[i] + c * Cq[i];
    }

    std::vector<double> Bp = B[p];
    std::vector<double> Bq = B[q];
    for (size_t i = 0; i < N; ++i) {
        B[i][p] = c * Bp[i] + s * Bq[i];
        B[i][q] = -s * Bp[i] + c * Bq[i];
    }

    C[p][p] = c * c * Cp[p] + 2 * s * c * C[p][q] + s * s * Cq[q];
    C[q][q] = s * s * Cp[p] - 2 * s * c * C[p][q] + c * c * Cq[q];
    C[p][q] = 0.0;
    C[q][p] = 0.0;
}

ParameterOptimizer::ParameterOptimizer() : algorithm_(Algorithm::NONE) {}

auto ParameterOptimizer::set_algorithm(Algorithm algo) -> void { algorithm_ = algo; }

auto ParameterOptimizer::get_algorithm() const -> Algorithm { return algorithm_; }

auto ParameterOptimizer::optimize(FitnessFunction fitness,
                                  const ParameterBounds& bounds,
                                  AlgorithmID algo_id,
                                  uint64_t max_time_ms) -> OptimizationResult {
    (void)algo_id;
    switch (algorithm_) {
        case Algorithm::GENETIC_ALGORITHM: {
            GAConfig cfg;
            cfg.max_time_ms = max_time_ms;
            return ga_.optimize(fitness, bounds, cfg);
        }
        case Algorithm::PARTICLE_SWARM: {
            PSOConfig cfg;
            cfg.max_time_ms = max_time_ms;
            return pso_.optimize(fitness, bounds, cfg);
        }
        case Algorithm::CMA_ES: {
            CMAESConfig cfg;
            cfg.max_time_ms = max_time_ms;
            return cmaes_.optimize(fitness, bounds, cfg);
        }
        default: {
            OptimizationResult result;
            result.algorithm_name = "None";
            std::mt19937 default_rng(42);
            result.best_params = bounds.random_params(default_rng);
            result.best_fitness = fitness(result.best_params);
            return result;
        }
    }
}

auto ParameterOptimizer::to_json() const -> std::string {
    std::ostringstream json;
    json << "{\n";
    json << "  \"algorithm\": " << static_cast<int>(algorithm_) << ",\n";
    json << "  \"algorithm_name\": \"" << algorithm_to_string(algorithm_) << "\"\n";
    json << "}\n";
    return json.str();
}

auto ParameterOptimizer::algorithm_to_string(Algorithm algo) -> std::string {
    switch (algo) {
        case Algorithm::GENETIC_ALGORITHM:
            return "GeneticAlgorithm";
        case Algorithm::PARTICLE_SWARM:
            return "ParticleSwarmOptimization";
        case Algorithm::CMA_ES:
            return "CMA-ES";
        case Algorithm::NONE:
        default:
            return "None";
    }
}

}  // namespace ade
}  // namespace compressor

#pragma once

#include <cstdint>
#include <memory>
#include <ostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace compressor {
namespace ade {

// ========================================================================
// Training Sample
// ========================================================================
struct TrainingSample {
    std::vector<float> features;
    int label{0};
    float weight{1.0f};
};

// ========================================================================
// Configurations
// ========================================================================
struct DecisionTreeConfig {
    size_t max_depth{20};
    size_t min_samples_split{2};
    size_t min_samples_leaf{1};
    size_t max_features{0}; // 0 means use sqrt(num_features)
    size_t random_seed{42};
};

struct RandomForestConfig {
    size_t num_trees{50};
    size_t max_depth{20};
    size_t min_samples_split{2};
    size_t min_samples_leaf{1};
    size_t max_features{0}; // 0 means use sqrt(num_features)
    size_t random_seed{42};
    bool bootstrap{true};
    double bootstrap_ratio{1.0};
};

// ========================================================================
// DecisionTree
// ========================================================================
class DecisionTree {
public:
    DecisionTree() = default;
    ~DecisionTree() = default;

    DecisionTree(DecisionTree&&) = default;
    DecisionTree(const DecisionTree&) = delete;
    auto operator=(DecisionTree&&) -> DecisionTree& = default;
    auto operator=(const DecisionTree&) -> DecisionTree& = delete;

    auto train(const std::vector<TrainingSample>& samples,
               const DecisionTreeConfig& config) -> void;

    auto predict(const std::vector<float>& features) const -> int;

    auto predict_proba(const std::vector<float>& features) const -> std::vector<float>;

    auto feature_importance() const -> std::vector<float>;

    auto to_json() const -> std::string;

    auto save(std::ostream& os) const -> bool;
    auto save(const std::string& filepath) const -> bool;

    auto load(std::istream& is) -> bool;
    auto load(const std::string& filepath) -> bool;

private:
    struct Node {
        bool is_leaf{true};
        int predicted_class{0};
        std::vector<float> class_distribution;

        size_t feature_index{0};
        float threshold{0.0f};
        double split_gain{0.0};

        // For non-leaf nodes
        std::unique_ptr<Node> left;
        std::unique_ptr<Node> right;
    };

    struct SplitResult {
        size_t feature_index{0};
        float threshold{0.0f};
        double gain{0.0};
        std::vector<size_t> left_indices;
        std::vector<size_t> right_indices;
    };

    std::unique_ptr<Node> root_;
    DecisionTreeConfig config_;
    size_t num_features_{0};
    size_t num_classes_{0};
    size_t max_features_{0};
    std::vector<int> labels_;
    mutable std::mt19937 rng_;

    auto extract_labels(const std::vector<TrainingSample>& samples) const -> std::vector<int>;

    auto build_tree(const std::vector<TrainingSample>& samples,
                    const std::vector<size_t>& indices,
                    size_t depth) -> std::unique_ptr<Node>;

    auto is_pure(const std::vector<size_t>& indices) const -> bool;
    auto majority_class(const std::vector<size_t>& indices) const -> int;
    auto compute_distribution(const std::vector<size_t>& indices) const -> std::vector<float>;

    auto find_best_split(const std::vector<TrainingSample>& samples,
                         const std::vector<size_t>& indices) -> SplitResult;

    auto gini_impurity(const std::vector<size_t>& indices) const -> double;
    static auto gini_from_counts(const std::vector<size_t>& counts, size_t total) -> double;

    auto predict_node(const Node& node, const std::vector<float>& features) const -> int;
    auto predict_proba_node(const Node& node, const std::vector<float>& features) const
        -> std::vector<float>;
    auto accumulate_importance(const Node& node, std::vector<float>& importance) const -> void;

    auto node_to_json(std::ostringstream& json, const Node& node) const -> void;

    auto save_node(std::ostream& os, const Node& node) const -> void;
    auto load_node(std::istream& is, Node& node) -> void;
};

// ========================================================================
// RandomForest
// ========================================================================
class RandomForest {
public:
    RandomForest() = default;
    ~RandomForest() = default;

    RandomForest(RandomForest&&) = default;
    RandomForest(const RandomForest&) = delete;
    auto operator=(RandomForest&&) -> RandomForest& = default;
    auto operator=(const RandomForest&) -> RandomForest& = delete;

    auto train(const std::vector<TrainingSample>& samples,
               const RandomForestConfig& config) -> void;

    auto predict(const std::vector<float>& features) const -> int;

    auto predict_proba(const std::vector<float>& features) const -> std::vector<float>;

    auto feature_importance() const -> std::vector<float>;

    auto to_json() const -> std::string;

    auto save(std::ostream& os) const -> bool;
    auto save(const std::string& filepath) const -> bool;

    auto load(std::istream& is) -> bool;
    auto load(const std::string& filepath) -> bool;

    auto num_trees() const -> size_t { return trees_.size(); }

private:
    std::vector<DecisionTree> trees_;
    RandomForestConfig config_;
    size_t num_features_{0};

    auto bootstrap_sample(const std::vector<TrainingSample>& samples,
                          std::mt19937& rng,
                          const RandomForestConfig& config) const -> std::vector<TrainingSample>;
};

} // namespace ade
} // namespace compressor
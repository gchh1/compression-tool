#include "RandomForest.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <numeric>
#include <sstream>
#include <utility>

namespace compressor {
namespace ade {

auto DecisionTree::train(const std::vector<TrainingSample>& samples,
                         const DecisionTreeConfig& config) -> void {
    config_ = config;
    rng_.seed(config.random_seed);

    if (samples.empty()) return;

    num_features_ = samples[0].features.size();
    labels_ = extract_labels(samples);
    num_classes_ = *std::max_element(labels_.begin(), labels_.end()) + 1;

    if (config.max_features == 0 || config.max_features > num_features_) {
        max_features_ = static_cast<size_t>(std::sqrt(static_cast<double>(num_features_)));
        if (max_features_ == 0) max_features_ = num_features_;
    } else {
        max_features_ = config.max_features;
    }

    std::vector<size_t> indices(samples.size());
    std::iota(indices.begin(), indices.end(), 0);

    root_ = build_tree(samples, indices, 0);
}

auto DecisionTree::predict(const std::vector<float>& features) const -> int {
    if (!root_) return 0;
    return predict_node(*root_, features);
}

auto DecisionTree::predict_proba(const std::vector<float>& features) const
    -> std::vector<float> {
    if (!root_) return std::vector<float>(num_classes_, 1.0f / num_classes_);
    return predict_proba_node(*root_, features);
}

auto DecisionTree::feature_importance() const -> std::vector<float> {
    std::vector<float> importance(num_features_, 0.0f);
    if (root_) accumulate_importance(*root_, importance);

    float total = std::accumulate(importance.begin(), importance.end(), 0.0f);
    if (total > 0.0f) {
        for (auto& v : importance) v /= total;
    }
    return importance;
}

auto DecisionTree::to_json() const -> std::string {
    std::ostringstream json;
    json << "{\n";
    json << "  \"num_features\": " << num_features_ << ",\n";
    json << "  \"num_classes\": " << num_classes_ << ",\n";
    json << "  \"max_depth\": " << config_.max_depth << ",\n";
    json << "  \"tree\": ";
    if (root_) {
        node_to_json(json, *root_);
    } else {
        json << "null";
    }
    json << "\n}\n";
    return json.str();
}

auto DecisionTree::save(std::ostream& os) const -> bool {
    static const uint32_t MAGIC = 0x44545246;
    os.write(reinterpret_cast<const char*>(&MAGIC), 4);
    uint32_t version = 1;
    os.write(reinterpret_cast<const char*>(&version), 4);
    uint64_t nf = num_features_;
    uint64_t nc = num_classes_;
    os.write(reinterpret_cast<const char*>(&nf), 8);
    os.write(reinterpret_cast<const char*>(&nc), 8);
    if (root_) {
        uint8_t has_root = 1;
        os.write(reinterpret_cast<const char*>(&has_root), 1);
        save_node(os, *root_);
    } else {
        uint8_t has_root = 0;
        os.write(reinterpret_cast<const char*>(&has_root), 1);
    }
    return os.good();
}

auto DecisionTree::save(const std::string& filepath) const -> bool {
    std::ofstream ofs(filepath, std::ios::binary);
    if (!ofs) return false;
    return save(ofs);
}

auto DecisionTree::load(std::istream& is) -> bool {
    uint32_t magic = 0;
    is.read(reinterpret_cast<char*>(&magic), 4);
    if (magic != 0x44545246) return false;
    uint32_t version = 0;
    is.read(reinterpret_cast<char*>(&version), 4);
    if (version != 1) return false;
    uint64_t nf = 0;
    uint64_t nc = 0;
    is.read(reinterpret_cast<char*>(&nf), 8);
    is.read(reinterpret_cast<char*>(&nc), 8);
    num_features_ = static_cast<size_t>(nf);
    num_classes_ = static_cast<size_t>(nc);
    max_features_ = num_features_;
    uint8_t has_root = 0;
    is.read(reinterpret_cast<char*>(&has_root), 1);
    if (has_root) {
        root_ = std::make_unique<Node>();
        load_node(is, *root_);
    } else {
        root_.reset();
    }
    return is.good();
}

auto DecisionTree::load(const std::string& filepath) -> bool {
    std::ifstream ifs(filepath, std::ios::binary);
    if (!ifs) return false;
    return load(ifs);
}

auto DecisionTree::extract_labels(const std::vector<TrainingSample>& samples) const
    -> std::vector<int> {
    std::vector<int> lbls;
    lbls.reserve(samples.size());
    for (const auto& s : samples) lbls.push_back(s.label);
    return lbls;
}

auto DecisionTree::build_tree(const std::vector<TrainingSample>& samples,
                              const std::vector<size_t>& indices,
                              size_t depth) -> std::unique_ptr<Node> {
    auto node = std::make_unique<Node>();

    if (indices.size() <= config_.min_samples_split || depth >= config_.max_depth ||
        is_pure(indices)) {
        node->is_leaf = true;
        node->predicted_class = majority_class(indices);
        node->class_distribution = compute_distribution(indices);
        return node;
    }

    auto best_split = find_best_split(samples, indices);

    if (best_split.gain <= 0.0 || best_split.left_indices.empty() ||
        best_split.right_indices.empty()) {
        node->is_leaf = true;
        node->predicted_class = majority_class(indices);
        node->class_distribution = compute_distribution(indices);
        return node;
    }

    node->feature_index = best_split.feature_index;
    node->threshold = best_split.threshold;
    node->split_gain = best_split.gain;

    node->left = build_tree(samples, best_split.left_indices, depth + 1);
    node->right = build_tree(samples, best_split.right_indices, depth + 1);

    return node;
}

auto DecisionTree::is_pure(const std::vector<size_t>& indices) const -> bool {
    if (indices.size() <= 1) return true;
    int first_label = labels_[indices[0]];
    for (size_t i = 1; i < indices.size(); ++i) {
        if (labels_[indices[i]] != first_label) return false;
    }
    return true;
}

auto DecisionTree::majority_class(const std::vector<size_t>& indices) const -> int {
    std::vector<size_t> counts(num_classes_, 0);
    for (auto idx : indices) {
        if (labels_[idx] >= 0 && static_cast<size_t>(labels_[idx]) < num_classes_) {
            counts[labels_[idx]]++;
        }
    }
    return static_cast<int>(
        std::distance(counts.begin(), std::max_element(counts.begin(), counts.end())));
}

auto DecisionTree::compute_distribution(const std::vector<size_t>& indices) const
    -> std::vector<float> {
    std::vector<float> dist(num_classes_, 0.0f);
    for (auto idx : indices) {
        if (labels_[idx] >= 0 && static_cast<size_t>(labels_[idx]) < num_classes_) {
            dist[labels_[idx]] += 1.0f;
        }
    }
    float total = std::accumulate(dist.begin(), dist.end(), 0.0f);
    if (total > 0.0f) {
        for (auto& v : dist) v /= total;
    }
    return dist;
}

auto DecisionTree::find_best_split(const std::vector<TrainingSample>& samples,
                                   const std::vector<size_t>& indices) -> SplitResult {
    SplitResult best;
    best.gain = -1.0;

    double parent_impurity = gini_impurity(indices);

    std::vector<size_t> feature_indices(num_features_);
    std::iota(feature_indices.begin(), feature_indices.end(), 0);
    std::shuffle(feature_indices.begin(), feature_indices.end(), rng_);

    size_t features_to_try = std::min(max_features_, num_features_);

    for (size_t f = 0; f < features_to_try; ++f) {
        size_t fi = feature_indices[f];

        std::vector<std::pair<float, size_t>> sorted;
        sorted.reserve(indices.size());
        for (auto idx : indices) {
            sorted.emplace_back(samples[idx].features[fi], idx);
        }
        std::sort(sorted.begin(), sorted.end());

        std::vector<size_t> left_counts(num_classes_, 0);
        std::vector<size_t> right_counts(num_classes_, 0);
        size_t left_total = 0;
        size_t right_total = indices.size();

        for (auto idx : indices) {
            if (labels_[idx] >= 0 && static_cast<size_t>(labels_[idx]) < num_classes_) {
                right_counts[labels_[idx]]++;
            }
        }

        for (size_t i = 0; i < sorted.size() - 1; ++i) {
            size_t idx = sorted[i].second;
            int lbl = labels_[idx];

            if (lbl >= 0 && static_cast<size_t>(lbl) < num_classes_) {
                left_counts[lbl]++;
                right_counts[lbl]--;
            }
            left_total++;
            right_total--;

            if (left_total < config_.min_samples_leaf || right_total < config_.min_samples_leaf) {
                continue;
            }

            if (sorted[i].first == sorted[i + 1].first) continue;

            double left_gini = gini_from_counts(left_counts, left_total);
            double right_gini = gini_from_counts(right_counts, right_total);

            double weighted_gini = (left_total * left_gini + right_total * right_gini) /
                                   static_cast<double>(indices.size());

            double gain = parent_impurity - weighted_gini;

            if (gain > best.gain) {
                best.gain = gain;
                best.feature_index = fi;
                best.threshold = (sorted[i].first + sorted[i + 1].first) * 0.5f;

                best.left_indices.clear();
                best.right_indices.clear();
                for (size_t j = 0; j <= i; ++j) {
                    best.left_indices.push_back(sorted[j].second);
                }
                for (size_t j = i + 1; j < sorted.size(); ++j) {
                    best.right_indices.push_back(sorted[j].second);
                }
            }
        }
    }

    return best;
}

auto DecisionTree::gini_impurity(const std::vector<size_t>& indices) const -> double {
    if (indices.empty()) return 0.0;

    std::vector<size_t> counts(num_classes_, 0);
    for (auto idx : indices) {
        if (labels_[idx] >= 0 && static_cast<size_t>(labels_[idx]) < num_classes_) {
            counts[labels_[idx]]++;
        }
    }

    double gini = 1.0;
    double n = static_cast<double>(indices.size());
    for (auto c : counts) {
        if (c > 0) {
            double p = static_cast<double>(c) / n;
            gini -= p * p;
        }
    }
    return gini;
}

auto DecisionTree::gini_from_counts(const std::vector<size_t>& counts, size_t total) -> double {
    if (total == 0) return 0.0;
    double gini = 1.0;
    double n = static_cast<double>(total);
    for (auto c : counts) {
        if (c > 0) {
            double p = static_cast<double>(c) / n;
            gini -= p * p;
        }
    }
    return gini;
}

auto DecisionTree::predict_node(const Node& node, const std::vector<float>& features) const
    -> int {
    if (node.is_leaf) return node.predicted_class;
    if (features[node.feature_index] <= node.threshold) {
        return predict_node(*node.left, features);
    }
    return predict_node(*node.right, features);
}

auto DecisionTree::predict_proba_node(const Node& node, const std::vector<float>& features) const
    -> std::vector<float> {
    if (node.is_leaf) return node.class_distribution;
    if (features[node.feature_index] <= node.threshold) {
        return predict_proba_node(*node.left, features);
    }
    return predict_proba_node(*node.right, features);
}

auto DecisionTree::accumulate_importance(const Node& node, std::vector<float>& importance) const
    -> void {
    if (node.is_leaf) return;
    importance[node.feature_index] += static_cast<float>(node.split_gain);
    if (node.left) accumulate_importance(*node.left, importance);
    if (node.right) accumulate_importance(*node.right, importance);
}

auto DecisionTree::node_to_json(std::ostringstream& json, const Node& node) const -> void {
    if (node.is_leaf) {
        json << "{\"leaf\": true, \"class\": " << node.predicted_class << ", \"dist\": [";
        for (size_t i = 0; i < node.class_distribution.size(); ++i) {
            if (i > 0) json << ", ";
            json << node.class_distribution[i];
        }
        json << "]}";
    } else {
        json << "{\"leaf\": false, \"feature\": " << node.feature_index
             << ", \"threshold\": " << node.threshold << ", \"gain\": " << node.split_gain
             << ", \"left\": ";
        if (node.left)
            node_to_json(json, *node.left);
        else
            json << "null";
        json << ", \"right\": ";
        if (node.right)
            node_to_json(json, *node.right);
        else
            json << "null";
        json << "}";
    }
}

auto DecisionTree::save_node(std::ostream& os, const Node& node) const -> void {
    uint8_t is_leaf = node.is_leaf ? 1 : 0;
    os.write(reinterpret_cast<const char*>(&is_leaf), 1);

    if (node.is_leaf) {
        int32_t cls = node.predicted_class;
        os.write(reinterpret_cast<const char*>(&cls), 4);
        uint32_t dist_size = static_cast<uint32_t>(node.class_distribution.size());
        os.write(reinterpret_cast<const char*>(&dist_size), 4);
        os.write(reinterpret_cast<const char*>(node.class_distribution.data()),
                 dist_size * sizeof(float));
    } else {
        uint64_t fi = node.feature_index;
        os.write(reinterpret_cast<const char*>(&fi), 8);
        os.write(reinterpret_cast<const char*>(&node.threshold), sizeof(float));
        double gain = node.split_gain;
        os.write(reinterpret_cast<const char*>(&gain), sizeof(double));
        uint8_t has_left = node.left ? 1 : 0;
        uint8_t has_right = node.right ? 1 : 0;
        os.write(reinterpret_cast<const char*>(&has_left), 1);
        os.write(reinterpret_cast<const char*>(&has_right), 1);
        if (node.left) save_node(os, *node.left);
        if (node.right) save_node(os, *node.right);
    }
}

auto DecisionTree::load_node(std::istream& is, Node& node) -> void {
    uint8_t is_leaf = 0;
    is.read(reinterpret_cast<char*>(&is_leaf), 1);
    node.is_leaf = (is_leaf == 1);

    if (node.is_leaf) {
        int32_t cls = 0;
        is.read(reinterpret_cast<char*>(&cls), 4);
        node.predicted_class = cls;
        uint32_t dist_size = 0;
        is.read(reinterpret_cast<char*>(&dist_size), 4);
        node.class_distribution.resize(dist_size);
        is.read(reinterpret_cast<char*>(node.class_distribution.data()), dist_size * sizeof(float));
    } else {
        uint64_t fi = 0;
        is.read(reinterpret_cast<char*>(&fi), 8);
        node.feature_index = static_cast<size_t>(fi);
        is.read(reinterpret_cast<char*>(&node.threshold), sizeof(float));
        is.read(reinterpret_cast<char*>(&node.split_gain), sizeof(double));
        uint8_t has_left = 0;
        uint8_t has_right = 0;
        is.read(reinterpret_cast<char*>(&has_left), 1);
        is.read(reinterpret_cast<char*>(&has_right), 1);
        if (has_left) {
            node.left = std::make_unique<Node>();
            load_node(is, *node.left);
        }
        if (has_right) {
            node.right = std::make_unique<Node>();
            load_node(is, *node.right);
        }
    }
}

auto RandomForest::train(const std::vector<TrainingSample>& samples,
                         const RandomForestConfig& config) -> void {
    config_ = config;
    trees_.clear();
    trees_.reserve(config.num_trees);

    if (samples.empty()) return;

    std::mt19937 rng(config.random_seed);

    for (size_t t = 0; t < config.num_trees; ++t) {
        auto bootstrap_samples = bootstrap_sample(samples, rng, config);

        DecisionTreeConfig tree_config;
        tree_config.max_depth = config.max_depth;
        tree_config.min_samples_split = config.min_samples_split;
        tree_config.min_samples_leaf = config.min_samples_leaf;
        tree_config.max_features = config.max_features;
        tree_config.random_seed = config.random_seed + static_cast<size_t>(rng());

        DecisionTree tree;
        tree.train(bootstrap_samples, tree_config);
        trees_.push_back(std::move(tree));
    }
}

auto RandomForest::predict(const std::vector<float>& features) const -> int {
    if (trees_.empty()) return 0;

    std::unordered_map<int, size_t> votes;
    for (const auto& tree : trees_) {
        int pred = tree.predict(features);
        votes[pred]++;
    }

    int best_label = 0;
    size_t best_count = 0;
    for (const auto& [label, count] : votes) {
        if (count > best_count) {
            best_count = count;
            best_label = label;
        }
    }
    return best_label;
}

auto RandomForest::predict_proba(const std::vector<float>& features) const -> std::vector<float> {
    if (trees_.empty()) return {};

    std::vector<float> avg_proba;
    for (const auto& tree : trees_) {
        auto tree_proba = tree.predict_proba(features);
        if (avg_proba.empty()) {
            avg_proba = std::move(tree_proba);
        } else {
            for (size_t i = 0; i < avg_proba.size(); ++i) {
                avg_proba[i] += tree_proba[i];
            }
        }
    }

    float total = std::accumulate(avg_proba.begin(), avg_proba.end(), 0.0f);
    if (total > 0.0f) {
        for (auto& v : avg_proba) v /= total;
    }
    return avg_proba;
}

auto RandomForest::feature_importance() const -> std::vector<float> {
    if (trees_.empty()) return {};

    auto importance = trees_[0].feature_importance();
    for (size_t t = 1; t < trees_.size(); ++t) {
        auto tree_imp = trees_[t].feature_importance();
        for (size_t i = 0; i < importance.size(); ++i) {
            importance[i] += tree_imp[i];
        }
    }

    for (auto& v : importance) v /= static_cast<float>(trees_.size());
    return importance;
}

auto RandomForest::to_json() const -> std::string {
    std::ostringstream json;
    json << "{\n";
    json << "  \"num_trees\": " << trees_.size() << ",\n";
    json << "  \"config\": {\n";
    json << "    \"max_depth\": " << config_.max_depth << ",\n";
    json << "    \"min_samples_split\": " << config_.min_samples_split << ",\n";
    json << "    \"min_samples_leaf\": " << config_.min_samples_leaf << ",\n";
    json << "    \"max_features\": " << config_.max_features << "\n";
    json << "  },\n";
    json << "  \"feature_importance\": [";
    auto imp = feature_importance();
    for (size_t i = 0; i < imp.size(); ++i) {
        if (i > 0) json << ", ";
        json << imp[i];
    }
    json << "]\n}\n";
    return json.str();
}

auto RandomForest::save(std::ostream& os) const -> bool {
    static const uint32_t MAGIC = 0x52465346;
    os.write(reinterpret_cast<const char*>(&MAGIC), 4);
    uint32_t version = 1;
    os.write(reinterpret_cast<const char*>(&version), 4);

    uint64_t nt = trees_.size();
    os.write(reinterpret_cast<const char*>(&nt), 8);

    uint64_t cfg_max_depth = config_.max_depth;
    uint64_t cfg_min_split = config_.min_samples_split;
    uint64_t cfg_min_leaf = config_.min_samples_leaf;
    uint64_t cfg_max_feat = config_.max_features;
    uint64_t cfg_seed = config_.random_seed;
    uint8_t cfg_bootstrap = config_.bootstrap ? 1 : 0;
    double cfg_ratio = config_.bootstrap_ratio;

    os.write(reinterpret_cast<const char*>(&cfg_max_depth), 8);
    os.write(reinterpret_cast<const char*>(&cfg_min_split), 8);
    os.write(reinterpret_cast<const char*>(&cfg_min_leaf), 8);
    os.write(reinterpret_cast<const char*>(&cfg_max_feat), 8);
    os.write(reinterpret_cast<const char*>(&cfg_seed), 8);
    os.write(reinterpret_cast<const char*>(&cfg_bootstrap), 1);
    os.write(reinterpret_cast<const char*>(&cfg_ratio), sizeof(double));

    for (const auto& tree : trees_) {
        if (!tree.save(os)) return false;
    }
    return os.good();
}

auto RandomForest::save(const std::string& filepath) const -> bool {
    std::ofstream ofs(filepath, std::ios::binary);
    if (!ofs) return false;
    return save(ofs);
}

auto RandomForest::load(std::istream& is) -> bool {
    uint32_t magic = 0;
    is.read(reinterpret_cast<char*>(&magic), 4);
    if (magic != 0x52465346) return false;
    uint32_t version = 0;
    is.read(reinterpret_cast<char*>(&version), 4);
    if (version != 1) return false;

    uint64_t nt = 0;
    is.read(reinterpret_cast<char*>(&nt), 8);

    uint64_t cfg_max_depth = 0;
    uint64_t cfg_min_split = 0;
    uint64_t cfg_min_leaf = 0;
    uint64_t cfg_max_feat = 0;
    uint64_t cfg_seed = 0;
    uint8_t cfg_bootstrap = 0;
    double cfg_ratio = 0.0;

    is.read(reinterpret_cast<char*>(&cfg_max_depth), 8);
    is.read(reinterpret_cast<char*>(&cfg_min_split), 8);
    is.read(reinterpret_cast<char*>(&cfg_min_leaf), 8);
    is.read(reinterpret_cast<char*>(&cfg_max_feat), 8);
    is.read(reinterpret_cast<char*>(&cfg_seed), 8);
    is.read(reinterpret_cast<char*>(&cfg_bootstrap), 1);
    is.read(reinterpret_cast<char*>(&cfg_ratio), sizeof(double));

    config_.num_trees = static_cast<size_t>(nt);
    config_.max_depth = static_cast<size_t>(cfg_max_depth);
    config_.min_samples_split = static_cast<size_t>(cfg_min_split);
    config_.min_samples_leaf = static_cast<size_t>(cfg_min_leaf);
    config_.max_features = static_cast<size_t>(cfg_max_feat);
    config_.random_seed = static_cast<size_t>(cfg_seed);
    config_.bootstrap = (cfg_bootstrap == 1);
    config_.bootstrap_ratio = cfg_ratio;

    trees_.clear();
    trees_.reserve(nt);
    for (size_t i = 0; i < nt; ++i) {
        DecisionTree tree;
        if (!tree.load(is)) return false;
        trees_.push_back(std::move(tree));
    }
    return is.good();
}

auto RandomForest::load(const std::string& filepath) -> bool {
    std::ifstream ifs(filepath, std::ios::binary);
    if (!ifs) return false;
    return load(ifs);
}

auto RandomForest::bootstrap_sample(const std::vector<TrainingSample>& samples,
                                    std::mt19937& rng,
                                    const RandomForestConfig& config) const
    -> std::vector<TrainingSample> {
    if (!config.bootstrap) return samples;

    size_t sample_size = static_cast<size_t>(samples.size() * config.bootstrap_ratio);
    if (sample_size == 0) sample_size = 1;

    std::uniform_int_distribution<size_t> dist(0, samples.size() - 1);

    std::vector<TrainingSample> bootstrap;
    bootstrap.reserve(sample_size);
    for (size_t i = 0; i < sample_size; ++i) {
        bootstrap.push_back(samples[dist(rng)]);
    }
    return bootstrap;
}

}  // namespace ade
}  // namespace compressor

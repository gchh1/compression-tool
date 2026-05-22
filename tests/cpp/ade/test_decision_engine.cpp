#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "RandomForest.hpp"
#include "DecisionEngine.hpp"
#include "ADEBridge.hpp"
#include "FeatureExtractorV3.hpp"

using namespace compressor::ade;

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    static void test_##name(); \
    struct TestRunner_##name { \
        TestRunner_##name() { \
            std::cout << "  [RUN ]  " #name << "... "; \
            try { \
                test_##name(); \
                ++tests_passed; \
                std::cout << "[PASS]\n"; \
            } catch (const std::exception& e) { \
                ++tests_failed; \
                std::cout << "[FAIL] " << e.what() << "\n"; \
            } catch (...) { \
                ++tests_failed; \
                std::cout << "[FAIL] Unknown exception\n"; \
            } \
            ++tests_run; \
        } \
    } runner_##name; \
    static void test_##name()

#define ASSERT_TRUE(cond) \
    if (!(cond)) throw std::runtime_error("Assertion failed: " #cond)

#define ASSERT_FALSE(cond) \
    if ((cond)) throw std::runtime_error("Assertion failed (should be false): " #cond)

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) { \
        std::ostringstream oss; \
        oss << "Expected " << static_cast<int>(b) << " but got " << static_cast<int>(a); \
        throw std::runtime_error(oss.str()); \
    }

#define ASSERT_NEAR(a, b, eps) \
    if (std::abs((a) - (b)) > (eps)) { \
        std::ostringstream oss; \
        oss << "Expected ~" << (b) << " but got " << (a) << " (eps=" << (eps) << ")"; \
        throw std::runtime_error(oss.str()); \
    }

// ========================================================================
// HELPER: Generate synthetic training data
// ========================================================================

static auto generate_linearly_separable(size_t n_samples = 200,
                                        size_t n_features = 10,
                                        size_t seed = 42)
    -> std::vector<TrainingSample> {
    std::mt19937 rng(seed);
    std::normal_distribution<float> dist_pos(2.0f, 1.0f);
    std::normal_distribution<float> dist_neg(-2.0f, 1.0f);

    std::vector<TrainingSample> samples;
    samples.reserve(n_samples);

    for (size_t i = 0; i < n_samples; ++i) {
        TrainingSample s;
        s.features.resize(n_features);
        int label = (i < n_samples / 2) ? 0 : 1;

        auto& dist = (label == 0) ? dist_neg : dist_pos;
        for (size_t f = 0; f < n_features; ++f) {
            s.features[f] = dist(rng);
        }
        s.label = label;
        s.weight = 1.0f;
        samples.push_back(s);
    }
    return samples;
}

static auto generate_compression_features(size_t n_samples = 300,
                                           size_t seed = 42)
    -> std::vector<TrainingSample> {
    std::mt19937 rng(seed);
    std::vector<TrainingSample> samples;
    samples.reserve(n_samples);

    auto make_sample = [&](float entropy, float printable, float rle,
                           float dict, float skewness, int label) {
        TrainingSample s;
        s.features = {entropy / 8.0f,
                      printable,
                      rle,
                      dict,
                      (skewness + 3.0f) / 6.0f,
                      0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                      0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                      0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                      0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                      0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                      0.0f, 0.0f, 0.0f};
        s.label = label;
        s.weight = 1.0f;
        samples.push_back(s);
    };

    std::normal_distribution<float> noise(0.0f, 0.05f);

    for (size_t i = 0; i < n_samples / 5; ++i) {
        make_sample(1.0f + noise(rng), 0.1f, 0.1f, 0.9f, 2.0f,
                    algorithm_id_to_label(AlgorithmID::LZSS));
    }
    for (size_t i = 0; i < n_samples / 5; ++i) {
        make_sample(4.5f + noise(rng), 0.95f, 0.5f, 0.95f, -0.3f,
                    algorithm_id_to_label(AlgorithmID::DPFLATE));
    }
    for (size_t i = 0; i < n_samples / 5; ++i) {
        make_sample(5.5f + noise(rng), 0.7f, 0.4f, 0.8f, -0.2f,
                    algorithm_id_to_label(AlgorithmID::DEFLATE));
    }
    for (size_t i = 0; i < n_samples / 5; ++i) {
        make_sample(7.9f + noise(rng) * 0.1f, 0.95f, 0.95f, 0.5f, 0.01f,
                    algorithm_id_to_label(AlgorithmID::SKIP));
    }
    for (size_t i = 0; i < n_samples / 5; ++i) {
        make_sample(0.1f + noise(rng), 0.0f, 0.0f, 0.99f, 3.0f,
                    algorithm_id_to_label(AlgorithmID::LZSS));
    }

    return samples;
}

// ========================================================================
// DECISION TREE TESTS
// ========================================================================

TEST(dt_train_and_predict_simple) {
    auto samples = generate_linearly_separable(100, 4);

    DecisionTree tree;
    DecisionTreeConfig config;
    config.max_depth = 5;
    config.random_seed = 42;
    tree.train(samples, config);

    int correct = 0;
    for (const auto& s : samples) {
        if (tree.predict(s.features) == s.label) ++correct;
    }

    float accuracy = static_cast<float>(correct) / static_cast<float>(samples.size());
    ASSERT_TRUE(accuracy > 0.85f);
}

TEST(dt_predict_proba_dimensions) {
    auto samples = generate_linearly_separable(50, 4);

    DecisionTree tree;
    tree.train(samples);

    auto proba = tree.predict_proba(samples[0].features);
    ASSERT_EQ(proba.size(), 2u);

    float sum = std::accumulate(proba.begin(), proba.end(), 0.0f);
    ASSERT_NEAR(sum, 1.0f, 0.01f);
}

TEST(dt_feature_importance) {
    auto samples = generate_linearly_separable(100, 4);

    DecisionTree tree;
    tree.train(samples);

    auto importance = tree.feature_importance();
    ASSERT_EQ(importance.size(), 4u);

    float total = std::accumulate(importance.begin(), importance.end(), 0.0f);
    if (total > 0.0f) {
        for (auto& v : importance) v /= total;
    }

    float imp_sum = std::accumulate(importance.begin(), importance.end(), 0.0f);
    ASSERT_NEAR(imp_sum, 1.0f, 0.01f);
}

TEST(dt_empty_training) {
    std::vector<TrainingSample> empty;
    DecisionTree tree;
    tree.train(empty);

    std::vector<float> features(4, 0.0f);
    int pred = tree.predict(features);
    ASSERT_EQ(pred, 0);
}

TEST(dt_single_class) {
    std::vector<TrainingSample> samples(50);
    for (auto& s : samples) {
        s.features = {1.0f, 2.0f, 3.0f, 4.0f};
        s.label = 1;
        s.weight = 1.0f;
    }

    DecisionTree tree;
    tree.train(samples);

    ASSERT_EQ(tree.predict({1.0f, 2.0f, 3.0f, 4.0f}), 1);
}

TEST(dt_to_json) {
    auto samples = generate_linearly_separable(30, 4);
    DecisionTree tree;
    tree.train(samples);

    std::string json = tree.to_json();
    ASSERT_TRUE(json.find("num_features") != std::string::npos);
    ASSERT_TRUE(json.find("num_classes") != std::string::npos);
    ASSERT_TRUE(json.find("tree") != std::string::npos);
}

// ========================================================================
// RANDOM FOREST TESTS
// ========================================================================

TEST(rf_train_and_predict) {
    auto samples = generate_linearly_separable(200, 4);

    RandomForest rf;
    RandomForestConfig config;
    config.num_trees = 20;
    config.max_depth = 5;
    config.random_seed = 42;
    rf.train(samples, config);

    ASSERT_EQ(rf.num_trees(), 20u);

    int correct = 0;
    for (const auto& s : samples) {
        if (rf.predict(s.features) == s.label) ++correct;
    }

    float accuracy = static_cast<float>(correct) / static_cast<float>(samples.size());
    ASSERT_TRUE(accuracy > 0.90f);
}

TEST(rf_predict_proba) {
    auto samples = generate_linearly_separable(100, 4);

    RandomForest rf;
    RandomForestConfig config;
    config.num_trees = 10;
    rf.train(samples, config);

    auto proba = rf.predict_proba(samples[0].features);
    ASSERT_EQ(proba.size(), 2u);

    float sum = std::accumulate(proba.begin(), proba.end(), 0.0f);
    ASSERT_NEAR(sum, 1.0f, 0.01f);
}

TEST(rf_feature_importance) {
    auto samples = generate_linearly_separable(100, 4);

    RandomForest rf;
    RandomForestConfig config;
    config.num_trees = 10;
    rf.train(samples, config);

    auto importance = rf.feature_importance();
    ASSERT_EQ(importance.size(), 4u);

    float total = std::accumulate(importance.begin(), importance.end(), 0.0f);
    ASSERT_TRUE(total > 0.0f);
}

TEST(rf_compression_decision) {
    auto samples = generate_compression_features(300);

    RandomForest rf;
    RandomForestConfig config;
    config.num_trees = 30;
    config.max_depth = 8;
    config.random_seed = 42;
    rf.train(samples, config);

    std::vector<float> text_features = {
        4.5f / 8.0f, 0.95f, 0.5f, 0.95f, (-0.3f + 3.0f) / 6.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f
    };
    int pred = rf.predict(text_features);
    ASSERT_TRUE(pred == static_cast<int>(AlgorithmID::DPFLATE) ||
                pred == static_cast<int>(AlgorithmID::DEFLATE));

    std::vector<float> random_features = {
        7.9f / 8.0f, 0.95f, 0.95f, 0.5f, (0.01f + 3.0f) / 6.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f
    };
    int pred2 = rf.predict(random_features);
    ASSERT_EQ(pred2, static_cast<int>(AlgorithmID::SKIP));
}

TEST(rf_to_json) {
    auto samples = generate_linearly_separable(30, 4);
    RandomForest rf;
    RandomForestConfig config;
    config.num_trees = 5;
    rf.train(samples, config);

    std::string json = rf.to_json();
    ASSERT_TRUE(json.find("num_trees") != std::string::npos);
    ASSERT_TRUE(json.find("feature_importance") != std::string::npos);
}

TEST(rf_bootstrap_toggle) {
    auto samples = generate_linearly_separable(100, 4);

    RandomForestConfig config_no_bootstrap;
    config_no_bootstrap.num_trees = 5;
    config_no_bootstrap.bootstrap = false;

    RandomForest rf;
    rf.train(samples, config_no_bootstrap);
    ASSERT_EQ(rf.num_trees(), 5u);

    int pred = rf.predict(samples[0].features);
    ASSERT_TRUE(pred == 0 || pred == 1);
}

// ========================================================================
// RULE ENGINE TESTS
// ========================================================================

TEST(rule_low_entropy) {
    RuleEngine engine;

    FeatureVectorV3 fv;
    fv.reset();
    fv.base.shannon_entropy = 1.5f;
    fv.base.unique_byte_ratio = 0.1f;
    fv.base.rle_potential = 0.1f;
    fv.base.dict_potential = 0.9f;
    fv.base.longest_run_log2 = 0.6f;
    fv.base.zero_byte_ratio = 0.5f;
    fv.ext_type = ExtensionType::NONE;

    auto decision = engine.decide(fv);
    ASSERT_TRUE(decision.algorithm == AlgorithmID::LZSS ||
                decision.algorithm == AlgorithmID::DEFLATE);
    ASSERT_TRUE(decision.confidence > 0.0f);
}

TEST(rule_medium_entropy_text) {
    RuleEngine engine;

    FeatureVectorV3 fv;
    fv.reset();
    fv.base.shannon_entropy = 4.5f;
    fv.base.unique_byte_ratio = 0.4f;
    fv.base.printable_ratio = 0.95f;
    fv.base.dict_potential = 0.95f;
    fv.base.rle_potential = 0.5f;
    fv.ext_type = ExtensionType::TEXT;
    fv.ext_dim = 5;

    auto decision = engine.decide(fv);
    ASSERT_TRUE(decision.algorithm == AlgorithmID::DPFLATE ||
                decision.algorithm == AlgorithmID::DEFLATE);
}

TEST(rule_high_entropy_skip) {
    RuleEngine engine;

    FeatureVectorV3 fv;
    fv.reset();
    fv.base.shannon_entropy = 7.9f;
    fv.base.unique_byte_ratio = 0.95f;
    fv.base.skewness = 0.01f;
    fv.ext_type = ExtensionType::NONE;

    auto decision = engine.decide(fv);
    ASSERT_EQ(decision.algorithm, AlgorithmID::SKIP);
    ASSERT_TRUE(decision.confidence > 0.7f);
}

TEST(rule_multimedia_skip) {
    RuleEngine engine;

    FeatureVectorV3 fv;
    fv.reset();
    fv.base.shannon_entropy = 7.5f;
    fv.ext_type = ExtensionType::IMAGE;
    fv.ext_dim = 6;
    fv.ext.image.is_lossless_original = 0.0f;

    auto decision = engine.decide(fv);
    ASSERT_EQ(decision.algorithm, AlgorithmID::SKIP);
}

TEST(rule_archive_recompress) {
    RuleEngine engine;

    FeatureVectorV3 fv;
    fv.reset();
    fv.base.shannon_entropy = 7.8f;
    fv.base.unique_byte_ratio = 0.95f;
    fv.base.skewness = 0.01f;
    fv.ext_type = ExtensionType::ARCHIVE;
    fv.ext_dim = 5;
    fv.ext.archive.recompress_potential = 0.05f;

    auto decision = engine.decide(fv);
    ASSERT_EQ(decision.algorithm, AlgorithmID::SKIP);
}

TEST(rule_decision_with_explanation) {
    RuleEngine engine;

    FeatureVectorV3 fv;
    fv.reset();
    fv.base.shannon_entropy = 4.5f;
    fv.base.printable_ratio = 0.95f;
    fv.base.dict_potential = 0.95f;
    fv.ext_type = ExtensionType::TEXT;
    fv.ext_dim = 5;

    auto [decision, explanation] = engine.decide_with_explanation(fv);
    ASSERT_TRUE(explanation.find("Shannon Entropy") != std::string::npos);
    ASSERT_TRUE(explanation.find("Algorithm") != std::string::npos);
}

// ========================================================================
// ALGORITHM DECISION ENGINE TESTS
// ========================================================================

TEST(ade_rule_based_mode) {
    AlgorithmDecisionEngine engine;
    engine.set_mode(AlgorithmDecisionEngine::Mode::RULE_BASED);

    FeatureVectorV3 fv;
    fv.reset();
    fv.base.shannon_entropy = 7.9f;
    fv.base.unique_byte_ratio = 0.95f;
    fv.base.skewness = 0.01f;

    auto decision = engine.decide(fv);
    ASSERT_EQ(decision.algorithm, AlgorithmID::SKIP);
}

TEST(ade_ml_hybrid_fallback) {
    AlgorithmDecisionEngine engine;
    engine.set_mode(AlgorithmDecisionEngine::Mode::ML_HYBRID);

    ASSERT_FALSE(engine.is_ml_ready());

    FeatureVectorV3 fv;
    fv.reset();
    fv.base.shannon_entropy = 1.5f;
    fv.base.rle_potential = 0.1f;
    fv.base.dict_potential = 0.9f;

    auto decision = engine.decide(fv);
    ASSERT_TRUE(decision.algorithm != AlgorithmID::NONE);
}

TEST(ade_ml_only_fallback) {
    AlgorithmDecisionEngine engine;
    engine.set_mode(AlgorithmDecisionEngine::Mode::ML_ONLY);

    FeatureVectorV3 fv;
    fv.reset();
    fv.base.shannon_entropy = 4.5f;
    fv.base.printable_ratio = 0.95f;

    auto decision = engine.decide(fv);
    ASSERT_TRUE(decision.algorithm != AlgorithmID::NONE);
}

TEST(ade_train_and_predict) {
    AlgorithmDecisionEngine engine;

    auto samples = generate_compression_features(200);
    RandomForestConfig config;
    config.num_trees = 15;
    config.max_depth = 6;
    engine.train(samples, config);

    ASSERT_TRUE(engine.is_ml_ready());

    engine.set_mode(AlgorithmDecisionEngine::Mode::ML_ONLY);

    FeatureVectorV3 fv;
    fv.reset();
    fv.base.shannon_entropy = 7.9f;
    fv.base.unique_byte_ratio = 0.95f;
    fv.base.skewness = 0.01f;

    auto decision = engine.decide(fv);
    ASSERT_TRUE(decision.algorithm != AlgorithmID::NONE);
    ASSERT_TRUE(decision.confidence > 0.0f);
    ASSERT_TRUE(decision.reason.find("ML") != std::string::npos);
}

TEST(ade_hybrid_uses_best) {
    AlgorithmDecisionEngine engine;

    auto samples = generate_compression_features(200);
    RandomForestConfig config;
    config.num_trees = 20;
    config.max_depth = 6;
    engine.train(samples, config);

    engine.set_mode(AlgorithmDecisionEngine::Mode::ML_HYBRID);

    FeatureVectorV3 fv;
    fv.reset();
    fv.base.shannon_entropy = 1.0f;
    fv.base.rle_potential = 0.05f;
    fv.base.dict_potential = 0.95f;
    fv.base.longest_run_log2 = 0.6f;

    auto decision = engine.decide(fv);
    ASSERT_TRUE(decision.confidence > 0.0f);
}

TEST(ade_feature_importance_after_train) {
    AlgorithmDecisionEngine engine;

    auto samples = generate_compression_features(100);
    engine.train(samples);

    auto importance = engine.feature_importance();
    ASSERT_EQ(importance.size(), 33u);
    ASSERT_TRUE(std::accumulate(importance.begin(), importance.end(), 0.0f) > 0.0f);
}

TEST(ade_to_json) {
    AlgorithmDecisionEngine engine;
    std::string json = engine.to_json();
    ASSERT_TRUE(json.find("mode") != std::string::npos);
    ASSERT_TRUE(json.find("ml_ready") != std::string::npos);
}

// ========================================================================
// ADE BRIDGE TESTS
// ========================================================================

TEST(bridge_analyze_text) {
    ADEBridge bridge;

    std::string text(4096, 'A');
    for (size_t i = 0; i < text.size(); ++i) {
        text[i] = "Hello World! This is a test. "[i % 30];
    }
    std::vector<uint8_t> data(text.begin(), text.end());

    auto result = bridge.analyze(data);
    ASSERT_TRUE(result.algorithm != CoreAlgorithmID::None);
    ASSERT_TRUE(result.shannon_entropy > 0.0f);
    ASSERT_TRUE(result.extraction_time_ms >= 0.0);
}

TEST(bridge_analyze_empty) {
    ADEBridge bridge;

    std::vector<uint8_t> empty;
    auto result = bridge.analyze(empty);
    ASSERT_EQ(result.algorithm, CoreAlgorithmID::None);
}

TEST(bridge_analyze_random) {
    ADEBridge bridge;

    std::vector<uint8_t> random_data(4096);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& b : random_data) b = static_cast<uint8_t>(dist(rng));

    auto result = bridge.analyze(random_data);
    ASSERT_TRUE(result.shannon_entropy > 7.0f);
}

TEST(bridge_map_algorithm) {
    ASSERT_EQ(ADEBridge::map_algorithm(AlgorithmID::DEFLATE),
              CoreAlgorithmID::Deflate);
    ASSERT_EQ(ADEBridge::map_algorithm(AlgorithmID::LZSS),
              CoreAlgorithmID::LZSS);
    ASSERT_EQ(ADEBridge::map_algorithm(AlgorithmID::LZDP),
              CoreAlgorithmID::LZDP);
    ASSERT_EQ(ADEBridge::map_algorithm(AlgorithmID::DPFLATE),
              CoreAlgorithmID::DPFlate);
    ASSERT_EQ(ADEBridge::map_algorithm(AlgorithmID::GZIP),
              CoreAlgorithmID::Deflate);
    ASSERT_EQ(ADEBridge::map_algorithm(AlgorithmID::SKIP),
              CoreAlgorithmID::None);
    ASSERT_EQ(ADEBridge::map_algorithm(AlgorithmID::NONE),
              CoreAlgorithmID::None);
}

TEST(bridge_reverse_map) {
    ASSERT_EQ(ADEBridge::map_algorithm(CoreAlgorithmID::Deflate),
              AlgorithmID::DEFLATE);
    ASSERT_EQ(ADEBridge::map_algorithm(CoreAlgorithmID::LZSS),
              AlgorithmID::LZSS);
    ASSERT_EQ(ADEBridge::map_algorithm(CoreAlgorithmID::LZDP),
              AlgorithmID::LZDP);
    ASSERT_EQ(ADEBridge::map_algorithm(CoreAlgorithmID::DPFlate),
              AlgorithmID::DPFLATE);
}

TEST(bridge_set_mode) {
    ADEBridge bridge;
    bridge.set_mode(AlgorithmDecisionEngine::Mode::ML_HYBRID);
    ASSERT_EQ(bridge.get_mode(), AlgorithmDecisionEngine::Mode::ML_HYBRID);

    bridge.set_mode(AlgorithmDecisionEngine::Mode::ML_ONLY);
    ASSERT_EQ(bridge.get_mode(), AlgorithmDecisionEngine::Mode::ML_ONLY);
}

TEST(bridge_analyze_repetitive) {
    ADEBridge bridge;

    std::vector<uint8_t> repetitive(4096, 0xAA);
    auto result = bridge.analyze(repetitive);
    ASSERT_TRUE(result.shannon_entropy < 1.0f);
    ASSERT_TRUE(result.algorithm == CoreAlgorithmID::LZSS ||
                result.algorithm == CoreAlgorithmID::Deflate);
}

// ========================================================================
// END-TO-END: FEATURE EXTRACTION → ADE DECISION
// ========================================================================

TEST(e2e_text_to_decision) {
    FeatureExtractorV3 extractor;
    AlgorithmDecisionEngine engine;

    std::string text = "The quick brown fox jumps over the lazy dog. ";
    std::vector<uint8_t> data;
    for (size_t i = 0; i < 200; ++i) {
        data.insert(data.end(), text.begin(), text.end());
    }

    auto result = extractor.extract(data);
    auto decision = engine.decide(result.vector);

    ASSERT_TRUE(decision.algorithm == AlgorithmID::DPFLATE ||
                decision.algorithm == AlgorithmID::DEFLATE);
    ASSERT_TRUE(decision.confidence > 0.0f);
    ASSERT_TRUE(decision.estimated_ratio < 1.0f);
}

TEST(e2e_random_to_skip) {
    FeatureExtractorV3 extractor;
    AlgorithmDecisionEngine engine;

    std::vector<uint8_t> random_data(4096);
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& b : random_data) b = static_cast<uint8_t>(dist(rng));

    auto result = extractor.extract(random_data);
    auto decision = engine.decide(result.vector);

    ASSERT_EQ(decision.algorithm, AlgorithmID::SKIP);
}

TEST(e2e_bridge_full_pipeline) {
    ADEBridge bridge;

    std::string text = "function main() { return 0; } ";
    std::vector<uint8_t> data;
    for (size_t i = 0; i < 200; ++i) {
        data.insert(data.end(), text.begin(), text.end());
    }

    auto result = bridge.analyze(data);
    ASSERT_TRUE(result.algorithm != CoreAlgorithmID::None);
    ASSERT_TRUE(result.confidence > 0.0f);
    ASSERT_FALSE(result.file_type.empty());
}

// ========================================================================
// MODEL PERSISTENCE TESTS
// ========================================================================

TEST(dt_save_load_roundtrip) {
    auto samples = generate_compression_features(100);
    DecisionTree tree;
    DecisionTreeConfig config;
    config.max_depth = 6;
    tree.train(samples, config);

    std::vector<float> test_input = samples[0].features;
    int pred_before = tree.predict(test_input);

    std::string path = "test_dt_model.bin";
    ASSERT_TRUE(tree.save(path));

    DecisionTree tree2;
    ASSERT_TRUE(tree2.load(path));

    int pred_after = tree2.predict(test_input);
    ASSERT_EQ(pred_before, pred_after);

    auto proba_before = tree.predict_proba(test_input);
    auto proba_after = tree2.predict_proba(test_input);
    ASSERT_EQ(proba_before.size(), proba_after.size());
    for (size_t i = 0; i < proba_before.size(); ++i) {
        ASSERT_NEAR(proba_before[i], proba_after[i], 1e-5f);
    }

    std::remove(path.c_str());
}

TEST(rf_save_load_roundtrip) {
    auto samples = generate_compression_features(100);
    RandomForest rf;
    RandomForestConfig config;
    config.num_trees = 10;
    config.max_depth = 6;
    config.random_seed = 42;
    rf.train(samples, config);

    std::vector<float> test_input = samples[0].features;
    int pred_before = rf.predict(test_input);

    std::string path = "test_rf_model.bin";
    ASSERT_TRUE(rf.save(path));

    RandomForest rf2;
    ASSERT_TRUE(rf2.load(path));
    ASSERT_TRUE(rf2.is_trained());
    ASSERT_EQ(rf2.num_trees(), 10u);

    int pred_after = rf2.predict(test_input);
    ASSERT_EQ(pred_before, pred_after);

    auto proba_before = rf.predict_proba(test_input);
    auto proba_after = rf2.predict_proba(test_input);
    ASSERT_EQ(proba_before.size(), proba_after.size());
    for (size_t i = 0; i < proba_before.size(); ++i) {
        ASSERT_NEAR(proba_before[i], proba_after[i], 1e-5f);
    }

    auto imp_before = rf.feature_importance();
    auto imp_after = rf2.feature_importance();
    ASSERT_EQ(imp_before.size(), imp_after.size());
    for (size_t i = 0; i < imp_before.size(); ++i) {
        ASSERT_NEAR(imp_before[i], imp_after[i], 1e-5f);
    }

    std::remove(path.c_str());
}

TEST(engine_save_load_model) {
    AlgorithmDecisionEngine engine;
    auto samples = generate_compression_features(100);
    RandomForestConfig config;
    config.num_trees = 10;
    config.max_depth = 6;
    engine.train(samples, config);
    ASSERT_TRUE(engine.is_ml_ready());

    std::string path = "test_engine_model.bin";
    ASSERT_TRUE(engine.save_model(path));

    AlgorithmDecisionEngine engine2;
    ASSERT_TRUE(engine2.load_model(path));
    ASSERT_TRUE(engine2.is_ml_ready());

    FeatureVectorV3 fv;
    fv.reset();
    fv.base.shannon_entropy = 4.5f;
    fv.base.printable_ratio = 0.95f;
    fv.base.dict_potential = 0.9f;

    engine.set_mode(AlgorithmDecisionEngine::Mode::ML_ONLY);
    engine2.set_mode(AlgorithmDecisionEngine::Mode::ML_ONLY);

    auto d1 = engine.decide(fv);
    auto d2 = engine2.decide(fv);
    ASSERT_EQ(static_cast<int>(d1.algorithm), static_cast<int>(d2.algorithm));

    std::remove(path.c_str());
}

TEST(bridge_save_load_model) {
    ADEBridge bridge;
    auto samples = generate_compression_features(100);
    RandomForestConfig config;
    config.num_trees = 10;
    config.max_depth = 6;
    bridge.train(samples, config);
    ASSERT_TRUE(bridge.is_ml_ready());

    std::string path = "test_bridge_model.bin";
    ASSERT_TRUE(bridge.save_model(path));

    ADEBridge bridge2;
    ASSERT_TRUE(bridge2.load_model(path));
    ASSERT_TRUE(bridge2.is_ml_ready());

    std::string text = "Hello World! " ;
    std::vector<uint8_t> data;
    for (size_t i = 0; i < 100; ++i) {
        data.insert(data.end(), text.begin(), text.end());
    }

    bridge.set_mode(AlgorithmDecisionEngine::Mode::ML_HYBRID);
    bridge2.set_mode(AlgorithmDecisionEngine::Mode::ML_HYBRID);

    auto r1 = bridge.analyze(data);
    auto r2 = bridge2.analyze(data);
    ASSERT_EQ(static_cast<int>(r1.algorithm), static_cast<int>(r2.algorithm));

    std::remove(path.c_str());
}

TEST(rf_load_invalid_file) {
    RandomForest rf;
    ASSERT_FALSE(rf.load("nonexistent_file.bin"));

    std::string path = "test_invalid.bin";
    { std::ofstream ofs(path); ofs << "garbage data"; }
    ASSERT_FALSE(rf.load(path));
    std::remove(path.c_str());
}

// ========================================================================
// MAIN
// ========================================================================

auto main() -> int {
    std::cout << "\n========================================\n";
    std::cout << "  ADE Decision Engine & Random Forest\n";
    std::cout << "  Unit Test Suite\n";
    std::cout << "========================================\n\n";

    std::cout << "Passed: " << tests_passed << " \u2713\n";
    std::cout << "Failed: " << tests_failed << " \u2717\n";

    if (tests_failed > 0) {
        std::cout << "\u274c SOME TESTS FAILED!\n";
        return 1;
    }
    std::cout << "\u2705 ALL TESTS PASSED!\n";
    return 0;
}

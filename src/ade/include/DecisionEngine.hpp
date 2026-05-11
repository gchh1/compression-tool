#pragma once

#include "FeatureExtractorV3.hpp"
#include "RandomForest.hpp"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <utility>
#include <vector>

namespace compressor {
namespace ade {

enum class AlgorithmID : uint8_t {
    NONE = 0,
    DEFLATE,
    LZSS,
    LZDP,
    DPFLATE,
    GZIP,
    DELTA_DEFLATE,
    BROTLI,
    ZSTD,
    SKIP
};

auto algorithm_id_to_string(AlgorithmID id) -> std::string;

struct AlgorithmParams {
    size_t window_size{32768};
    size_t min_match{3};
    size_t max_chain_length{256};
    size_t lookahead_size{256};
    size_t dp_range{3};

    auto to_string() const -> std::string;
};

struct CompressionDecision {
    AlgorithmID algorithm{AlgorithmID::NONE};
    AlgorithmParams params;
    float estimated_ratio{1.0f};
    float confidence{0.0f};
    std::string reason;

    auto to_json() const -> std::string;
};

class RuleEngine {
public:
    auto decide(const FeatureVectorV3& features) const -> CompressionDecision;
    auto decide_with_explanation(const FeatureVectorV3& features) const
        -> std::pair<CompressionDecision, std::string>;

private:
    auto is_already_compressed(const FeatureVectorV3& features) const -> bool;
    auto is_multimedia_already_compressed(const FeatureVectorV3& features) const -> bool;
    auto decide_low_entropy(const FeatureVectorV3& features) const -> CompressionDecision;
    auto decide_medium_entropy(const FeatureVectorV3& features) const -> CompressionDecision;
    auto decide_high_entropy(const FeatureVectorV3& features) const -> CompressionDecision;
    auto decide_very_high_entropy(const FeatureVectorV3& features) const -> CompressionDecision;
    auto detect_dominant_type(const FeatureVectorV3& features) const -> uint8_t;
};

auto algorithm_id_to_label(AlgorithmID id) -> int;

auto label_to_algorithm_id(int label) -> AlgorithmID;

class AlgorithmDecisionEngine {
public:
    enum class Mode : uint8_t {
        RULE_BASED = 0,
        ML_HYBRID,
        ML_ONLY
    };

    AlgorithmDecisionEngine();

    auto set_mode(Mode mode) -> void;

    auto get_mode() const -> Mode;

    auto train(const std::vector<TrainingSample>& samples,
               const RandomForestConfig& config = {}) -> void;

    auto is_ml_ready() const -> bool;

    auto decide(const FeatureVectorV3& features) const -> CompressionDecision;

    auto decide_with_explanation(const FeatureVectorV3& features) const
        -> std::pair<CompressionDecision, std::string>;

    auto feature_importance() const -> std::vector<float>;

    auto to_json() const -> std::string;

    auto save_model(const std::string& filepath) const -> bool;
    auto load_model(const std::string& filepath) -> bool;

    auto save_model(std::ostream& os) const -> bool;
    auto load_model(std::istream& is) -> bool;

    static auto features_to_vector(const FeatureVectorV3& features) -> std::vector<float>;
    static auto sample_from_result(const ExtractionResult& result,
                                   AlgorithmID correct_algorithm) -> TrainingSample;

private:
    Mode mode_;
    RuleEngine rule_engine_;
    RandomForest rf_;
    bool rf_trained_;

    auto decide_ml(const FeatureVectorV3& features) const -> CompressionDecision;
    static auto default_params_for_algorithm(AlgorithmID algo) -> AlgorithmParams;
    static auto estimate_ratio(const FeatureVectorV3& features, AlgorithmID algo) -> float;
};

}  // namespace ade
}  // namespace compressor

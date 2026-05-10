#pragma once

#include "FeatureExtractorV3.hpp"
#include "RandomForest.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace compressor {
namespace ade {

enum class AlgorithmID : uint8_t {
    NONE = 0,
    DEFLATE,
    LZSS,
    LZMINE,
    DPFLATE,
    GZIP,
    DELTA_DEFLATE,
    BROTLI,
    ZSTD,
    SKIP
};

inline auto algorithm_id_to_string(AlgorithmID id) -> std::string {
    switch (id) {
        case AlgorithmID::NONE: return "NONE";
        case AlgorithmID::DEFLATE: return "DEFLATE";
        case AlgorithmID::LZSS: return "LZSS";
        case AlgorithmID::LZMINE: return "LZMINE";
        case AlgorithmID::DPFLATE: return "DPFLATE";
        case AlgorithmID::GZIP: return "GZIP";
        case AlgorithmID::DELTA_DEFLATE: return "DELTA+DEFLATE";
        case AlgorithmID::BROTLI: return "BROTLI";
        case AlgorithmID::ZSTD: return "ZSTD";
        case AlgorithmID::SKIP: return "SKIP";
        default: return "UNKNOWN";
    }
}

struct AlgorithmParams {
    size_t window_size{32768};
    size_t min_match{3};
    size_t max_chain_length{256};
    size_t lookahead_size{256};
    size_t dp_range{3};

    auto to_string() const -> std::string {
        std::ostringstream s;
        s << "window=" << window_size
          << " min_match=" << min_match
          << " chain=" << max_chain_length
          << " lookahead=" << lookahead_size
          << " dp=" << dp_range;
        return s.str();
    }
};

struct CompressionDecision {
    AlgorithmID algorithm{AlgorithmID::NONE};
    AlgorithmParams params;
    float estimated_ratio{1.0f};
    float confidence{0.0f};
    std::string reason;

    auto to_json() const -> std::string {
        std::ostringstream json;
        json << "{\n";
        json << "  \"algorithm\": \"" << algorithm_id_to_string(algorithm) << "\",\n";
        json << "  \"params\": {\n";
        json << "    \"window_size\": " << params.window_size << ",\n";
        json << "    \"min_match\": " << params.min_match << ",\n";
        json << "    \"max_chain_length\": " << params.max_chain_length << ",\n";
        json << "    \"lookahead_size\": " << params.lookahead_size << ",\n";
        json << "    \"dp_range\": " << params.dp_range << "\n";
        json << "  },\n";
        json << "  \"estimated_ratio\": " << estimated_ratio << ",\n";
        json << "  \"confidence\": " << confidence << ",\n";
        json << "  \"reason\": \"" << reason << "\"\n";
        json << "}\n";
        return json.str();
    }
};

class RuleEngine {
public:
    auto decide(const FeatureVectorV3& features) const -> CompressionDecision {
        CompressionDecision decision;

        const auto& base = features.base;

        float entropy = base.shannon_entropy;
        float unique_byte = base.unique_byte_ratio;
        float printable = base.printable_ratio;
        float rle_pot = base.rle_potential;
        float dict_pot = base.dict_potential;
        float skewness_val = base.skewness;
        float bigram_uniq = base.unique_bigram_ratio;
        float size_log2 = base.file_size_log2;
        float zero_ratio = base.zero_byte_ratio;
        float local_var = base.local_entropy_var;

        (void)unique_byte;
        (void)printable;
        (void)rle_pot;
        (void)dict_pot;
        (void)skewness_val;
        (void)bigram_uniq;
        (void)zero_ratio;
        (void)local_var;

        size_t file_size = static_cast<size_t>(std::exp2(size_log2 * 32.0f));
        (void)file_size;

        if (is_already_compressed(features)) {
            decision.algorithm = AlgorithmID::SKIP;
            decision.estimated_ratio = 0.98f;
            decision.confidence = 0.85f;
            decision.reason = "File appears already compressed/encrypted (entropy="
                + std::to_string(entropy) + ")";
            return decision;
        }

        if (is_multimedia_already_compressed(features)) {
            decision.algorithm = AlgorithmID::SKIP;
            decision.estimated_ratio = 0.99f;
            decision.confidence = 0.90f;
            decision.reason = "Multimedia file already uses lossy compression";
            return decision;
        }

        if (entropy < 3.0f) {
            return decide_low_entropy(features);
        }

        if (entropy < 5.5f) {
            return decide_medium_entropy(features);
        }

        if (entropy < 7.0f) {
            return decide_high_entropy(features);
        }

        return decide_very_high_entropy(features);
    }

    auto decide_with_explanation(const FeatureVectorV3& features) const
        -> std::pair<CompressionDecision, std::string> {
        auto decision = decide(features);

        std::ostringstream explanation;
        explanation << "=== ADE Decision Explanation ===\n";
        explanation << "Input Features:\n";
        explanation << "  Shannon Entropy: " << features.base.shannon_entropy << " bits/byte\n";
        explanation << "  Unique Byte Ratio: " << features.base.unique_byte_ratio << "\n";
        explanation << "  Printable Ratio: " << features.base.printable_ratio << "\n";
        explanation << "  RLE Potential: " << features.base.rle_potential << "\n";
        explanation << "  Dict Potential: " << features.base.dict_potential << "\n";
        explanation << "  Skewness: " << features.base.skewness << "\n";
        explanation << "  Bigram Uniqueness: " << features.base.unique_bigram_ratio << "\n";
        explanation << "  File Type: "
                    << file_type_to_string(
                           static_cast<FileType>(
                               detect_dominant_type(features)))
                    << "\n\n";
        explanation << "Decision:\n";
        explanation << "  Algorithm: " << algorithm_id_to_string(decision.algorithm) << "\n";
        explanation << "  Parameters: " << decision.params.to_string() << "\n";
        explanation << "  Estimated Ratio: " << decision.estimated_ratio << "\n";
        explanation << "  Confidence: " << decision.confidence << "\n";
        explanation << "  Reason: " << decision.reason << "\n";

        return {decision, explanation.str()};
    }

private:
    auto is_already_compressed(const FeatureVectorV3& features) const -> bool {
        const auto& base = features.base;
        float entropy = base.shannon_entropy;
        float unique_byte = base.unique_byte_ratio;
        float skewness = base.skewness;

        if (entropy > 7.5f && unique_byte > 0.9f && std::abs(skewness) < 0.15f) {
            return true;
        }

        if (features.ext_type == ExtensionType::ARCHIVE) {
            float recompress = features.ext.archive.recompress_potential;
            if (recompress < 0.1f) {
                return true;
            }
        }

        return false;
    }

    auto is_multimedia_already_compressed(const FeatureVectorV3& features) const -> bool {
        if (features.ext_type == ExtensionType::IMAGE) {
            if (features.ext.image.is_lossless_original < 0.5f) {
                return true;
            }
        }

        if (features.ext_type == ExtensionType::AUDIO) {
            if (features.ext.audio.is_lossless < 0.5f) {
                return true;
            }
        }

        if (features.ext_type == ExtensionType::VIDEO) {
            if (features.ext.video.is_lossless < 0.5f) {
                return true;
            }
        }

        return false;
    }

    auto decide_low_entropy(const FeatureVectorV3& features) const -> CompressionDecision {
        CompressionDecision decision;
        const auto& base = features.base;

        if (base.rle_potential < 0.3f) {
            decision.algorithm = AlgorithmID::LZSS;
            decision.params.window_size = 4096;
            decision.params.min_match = 3;
            decision.params.lookahead_size = 18;
            decision.estimated_ratio = 0.15f;
            decision.confidence = 0.80f;
            decision.reason = "Low entropy + high RLE potential → LZSS optimal for run-length data";
        } else if (base.dict_potential < 0.3f) {
            decision.algorithm = AlgorithmID::DEFLATE;
            decision.params.window_size = 32768;
            decision.params.min_match = 3;
            decision.params.max_chain_length = 128;
            decision.estimated_ratio = 0.20f;
            decision.confidence = 0.80f;
            decision.reason = "Low entropy + high dict potential → DEFLATE with large window";
        } else {
            decision.algorithm = AlgorithmID::DPFLATE;
            decision.params.window_size = 4096;
            decision.params.lookahead_size = 256;
            decision.params.dp_range = 3;
            decision.estimated_ratio = 0.25f;
            decision.confidence = 0.75f;
            decision.reason = "Low entropy → DPFLATE balanced compression";
        }

        return decision;
    }

    auto decide_medium_entropy(const FeatureVectorV3& features) const -> CompressionDecision {
        CompressionDecision decision;
        const auto& base = features.base;

        bool is_text = base.printable_ratio > 0.85f;

        if (is_text) {
            if (features.ext_type == ExtensionType::TEXT) {
                float syntax = features.ext.text.syntax_density;
                if (syntax > 0.15f) {
                    decision.algorithm = AlgorithmID::BROTLI;
                    decision.params.window_size = 65536;
                    decision.params.min_match = 3;
                    decision.params.max_chain_length = 256;
                    decision.estimated_ratio = 0.30f;
                    decision.confidence = 0.85f;
                    decision.reason = "Structured text/code → BROTLI with context modeling";
                } else {
                    decision.algorithm = AlgorithmID::DPFLATE;
                    decision.params.window_size = 8192;
                    decision.params.lookahead_size = 256;
                    decision.params.dp_range = 3;
                    decision.estimated_ratio = 0.38f;
                    decision.confidence = 0.80f;
                    decision.reason = "Natural language text → DPFLATE with DP optimization";
                }
            } else {
                decision.algorithm = AlgorithmID::BROTLI;
                decision.params.window_size = 65536;
                decision.params.min_match = 3;
                decision.params.max_chain_length = 256;
                decision.estimated_ratio = 0.32f;
                decision.confidence = 0.80f;
                decision.reason = "Printable text data → BROTLI context-aware compression";
            }
        } else {
            if (base.dict_potential < 0.5f) {
                decision.algorithm = AlgorithmID::LZMINE;
                decision.params.window_size = 8192;
                decision.params.lookahead_size = 256;
                decision.params.dp_range = 3;
                decision.estimated_ratio = 0.45f;
                decision.confidence = 0.70f;
                decision.reason = "Mixed content + dict potential → LZMINE with DP matching";
            } else {
                decision.algorithm = AlgorithmID::ZSTD;
                decision.params.window_size = 32768;
                decision.params.min_match = 3;
                decision.params.max_chain_length = 256;
                decision.estimated_ratio = 0.42f;
                decision.confidence = 0.75f;
                decision.reason = "Mixed content → ZSTD with adaptive compression";
            }
        }

        return decision;
    }

    auto decide_high_entropy(const FeatureVectorV3& features) const -> CompressionDecision {
        CompressionDecision decision;
        const auto& base = features.base;

        if (base.local_entropy_var > 0.5f) {
            decision.algorithm = AlgorithmID::ZSTD;
            decision.params.window_size = 32768;
            decision.params.min_match = 3;
            decision.params.max_chain_length = 256;
            decision.estimated_ratio = 0.60f;
            decision.confidence = 0.70f;
            decision.reason = "High entropy with local variation → ZSTD exploits structured regions";
        } else if (base.zero_byte_ratio > 0.15f) {
            decision.algorithm = AlgorithmID::DELTA_DEFLATE;
            decision.params.window_size = 32768;
            decision.params.min_match = 3;
            decision.estimated_ratio = 0.60f;
            decision.confidence = 0.60f;
            decision.reason = "High entropy + many zeros → Delta+DEFLATE for sparse data";
        } else {
            decision.algorithm = AlgorithmID::LZMINE;
            decision.params.window_size = 4096;
            decision.params.lookahead_size = 128;
            decision.params.dp_range = 2;
            decision.estimated_ratio = 0.75f;
            decision.confidence = 0.55f;
            decision.reason = "Uniform high entropy → LZMINE fast mode, limited gain expected";
        }

        return decision;
    }

    auto decide_very_high_entropy(const FeatureVectorV3& features) const -> CompressionDecision {
        CompressionDecision decision;
        const auto& base = features.base;

        float skewness = base.skewness;

        if (std::abs(skewness) > 0.2f) {
            decision.algorithm = AlgorithmID::DEFLATE;
            decision.params.window_size = 32768;
            decision.params.min_match = 3;
            decision.params.max_chain_length = 128;
            decision.estimated_ratio = 0.85f;
            decision.confidence = 0.45f;
            decision.reason = "Very high entropy with skewness → slight bias exploitable by DEFLATE";
        } else {
            decision.algorithm = AlgorithmID::LZSS;
            decision.params.window_size = 4096;
            decision.params.min_match = 3;
            decision.estimated_ratio = 0.95f;
            decision.confidence = 0.40f;
            decision.reason = "Near-random data → LZSS fast pass, minimal compression expected";
        }

        return decision;
    }

    auto detect_dominant_type(const FeatureVectorV3& features) const -> uint8_t {
        if (features.ext_type == ExtensionType::TEXT) return static_cast<uint8_t>(FileType::TEXT_PLAIN);
        if (features.ext_type == ExtensionType::IMAGE) return static_cast<uint8_t>(FileType::IMAGE_PNG);
        if (features.ext_type == ExtensionType::AUDIO) return static_cast<uint8_t>(FileType::AUDIO_WAV);
        if (features.ext_type == ExtensionType::VIDEO) return static_cast<uint8_t>(FileType::VIDEO_MP4);
        if (features.ext_type == ExtensionType::ARCHIVE) return static_cast<uint8_t>(FileType::ARCHIVE_ZIP);
        if (features.ext_type == ExtensionType::BINARY) return static_cast<uint8_t>(FileType::BINARY_GENERIC);
        return static_cast<uint8_t>(FileType::UNKNOWN);
    }
};

inline auto algorithm_id_to_label(AlgorithmID id) -> int {
    return static_cast<int>(id);
}

inline auto label_to_algorithm_id(int label) -> AlgorithmID {
    if (label >= 0 && label <= static_cast<int>(AlgorithmID::SKIP)) {
        return static_cast<AlgorithmID>(label);
    }
    return AlgorithmID::NONE;
}

class AlgorithmDecisionEngine {
public:
    enum class Mode : uint8_t {
        RULE_BASED = 0,
        ML_HYBRID,
        ML_ONLY
    };

    AlgorithmDecisionEngine() : mode_(Mode::RULE_BASED), rf_trained_(false) {}

    auto set_mode(Mode mode) -> void { mode_ = mode; }

    auto get_mode() const -> Mode { return mode_; }

    auto train(const std::vector<TrainingSample>& samples,
               const RandomForestConfig& config = {}) -> void {
        rf_.train(samples, config);
        rf_trained_ = true;
    }

    auto is_ml_ready() const -> bool { return rf_trained_; }

    auto decide(const FeatureVectorV3& features) const -> CompressionDecision {
        switch (mode_) {
            case Mode::RULE_BASED:
                return rule_engine_.decide(features);

            case Mode::ML_HYBRID: {
                auto rule_decision = rule_engine_.decide(features);
                if (!rf_trained_) return rule_decision;

                auto ml_decision = decide_ml(features);
                if (ml_decision.confidence > rule_decision.confidence) {
                    return ml_decision;
                }
                return rule_decision;
            }

            case Mode::ML_ONLY: {
                if (!rf_trained_) return rule_engine_.decide(features);
                return decide_ml(features);
            }

            default:
                return rule_engine_.decide(features);
        }
    }

    auto decide_with_explanation(const FeatureVectorV3& features) const
        -> std::pair<CompressionDecision, std::string> {
        auto decision = decide(features);

        std::ostringstream explanation;
        explanation << "=== ADE Decision Explanation ===\n";
        explanation << "Mode: ";
        switch (mode_) {
            case Mode::RULE_BASED: explanation << "Rule-Based"; break;
            case Mode::ML_HYBRID: explanation << "ML Hybrid (RF + Rules)"; break;
            case Mode::ML_ONLY: explanation << "ML Only (RF)"; break;
        }
        explanation << "\n";
        explanation << "ML Ready: " << (rf_trained_ ? "Yes" : "No") << "\n\n";

        explanation << "Input Features:\n";
        explanation << "  Shannon Entropy: " << features.base.shannon_entropy << "\n";
        explanation << "  Unique Byte Ratio: " << features.base.unique_byte_ratio << "\n";
        explanation << "  Printable Ratio: " << features.base.printable_ratio << "\n";
        explanation << "  RLE Potential: " << features.base.rle_potential << "\n";
        explanation << "  Dict Potential: " << features.base.dict_potential << "\n";
        explanation << "  Skewness: " << features.base.skewness << "\n\n";

        explanation << "Decision:\n";
        explanation << "  Algorithm: " << algorithm_id_to_string(decision.algorithm) << "\n";
        explanation << "  Parameters: " << decision.params.to_string() << "\n";
        explanation << "  Estimated Ratio: " << decision.estimated_ratio << "\n";
        explanation << "  Confidence: " << decision.confidence << "\n";
        explanation << "  Reason: " << decision.reason << "\n";

        return {decision, explanation.str()};
    }

    auto feature_importance() const -> std::vector<float> {
        if (!rf_trained_) return {};
        return rf_.feature_importance();
    }

    auto to_json() const -> std::string {
        std::ostringstream json;
        json << "{\n";
        json << "  \"mode\": " << static_cast<int>(mode_) << ",\n";
        json << "  \"ml_ready\": " << (rf_trained_ ? "true" : "false") << ",\n";
        json << "  \"num_trees\": " << rf_.num_trees() << "\n";
        json << "}\n";
        return json.str();
    }

    auto save_model(const std::string& filepath) const -> bool {
        if (!rf_trained_) return false;
        return rf_.save(filepath);
    }

    auto load_model(const std::string& filepath) -> bool {
        bool ok = rf_.load(filepath);
        if (ok) rf_trained_ = true;
        return ok;
    }

    auto save_model(std::ostream& os) const -> bool {
        if (!rf_trained_) return false;
        return rf_.save(os);
    }

    auto load_model(std::istream& is) -> bool {
        bool ok = rf_.load(is);
        if (ok) rf_trained_ = true;
        return ok;
    }

    static auto features_to_vector(const FeatureVectorV3& features) -> std::vector<float> {
        return features.to_padded_array(constants::MAX_PADDED_DIMENSIONS);
    }

    static auto sample_from_result(const ExtractionResult& result,
                                   AlgorithmID correct_algorithm) -> TrainingSample {
        TrainingSample sample;
        sample.features = result.vector.to_padded_array(constants::MAX_PADDED_DIMENSIONS);
        sample.label = algorithm_id_to_label(correct_algorithm);
        sample.weight = 1.0f;
        return sample;
    }

private:
    Mode mode_;
    RuleEngine rule_engine_;
    RandomForest rf_;
    bool rf_trained_;

    auto decide_ml(const FeatureVectorV3& features) const -> CompressionDecision {
        CompressionDecision decision;

        auto input = features.to_padded_array(constants::MAX_PADDED_DIMENSIONS);
        int predicted_label = rf_.predict(input);
        decision.algorithm = label_to_algorithm_id(predicted_label);

        auto proba = rf_.predict_proba(input);
        float max_proba = *std::max_element(proba.begin(), proba.end());
        decision.confidence = max_proba;

        decision.params = default_params_for_algorithm(decision.algorithm);
        decision.estimated_ratio = estimate_ratio(features, decision.algorithm);
        decision.reason = "ML prediction (RF, confidence=" +
                          std::to_string(max_proba) + ")";

        return decision;
    }

    static auto default_params_for_algorithm(AlgorithmID algo) -> AlgorithmParams {
        AlgorithmParams params;
        switch (algo) {
            case AlgorithmID::DEFLATE:
            case AlgorithmID::GZIP:
                params.window_size = 32768;
                params.min_match = 3;
                params.max_chain_length = 256;
                break;
            case AlgorithmID::LZSS:
                params.window_size = 4096;
                params.min_match = 3;
                params.lookahead_size = 18;
                break;
            case AlgorithmID::LZMINE:
                params.window_size = 8192;
                params.lookahead_size = 256;
                params.dp_range = 3;
                break;
            case AlgorithmID::DPFLATE:
                params.window_size = 4096;
                params.lookahead_size = 256;
                params.dp_range = 3;
                break;
            default:
                break;
        }
        return params;
    }

    static auto estimate_ratio(const FeatureVectorV3& features, AlgorithmID algo) -> float {
        float entropy = features.base.shannon_entropy;
        float base_ratio = 1.0f;

        if (entropy < 3.0f) base_ratio = 0.15f;
        else if (entropy < 5.5f) base_ratio = 0.35f;
        else if (entropy < 7.0f) base_ratio = 0.65f;
        else base_ratio = 0.90f;

        switch (algo) {
            case AlgorithmID::SKIP: return 0.99f;
            case AlgorithmID::DEFLATE:
            case AlgorithmID::GZIP:
                return base_ratio * 0.95f;
            case AlgorithmID::LZMINE:
            case AlgorithmID::DPFLATE:
                return base_ratio * 0.90f;
            case AlgorithmID::LZSS:
                return base_ratio * 1.05f;
            default:
                return base_ratio;
        }
    }
};

}  // namespace ade
}  // namespace compressor

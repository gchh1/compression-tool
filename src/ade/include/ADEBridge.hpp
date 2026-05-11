#pragma once

#include "DecisionEngine.hpp"
#include "FeatureExtractorV3.hpp"

#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef ADE_WITH_CORE
#include "AlgorithmFactory.hpp"
#endif

namespace compressor {
namespace ade {

#ifdef ADE_WITH_CORE
using CoreAlgorithmID = core::AlgorithmID;
#else
enum class CoreAlgorithmID {
    None,
    Deflate,
    Inflate,
    DeltaEncode,
    DeltaDecode,
    LZSS,
    LZSSDecompress,
    LZDP,
    LZDPDecompress,
    DPFlate,
    Brotli,
    BrotliDecompress,
    Zstd,
    ZstdDecompress,
};
#endif

struct ADEBridgeResult {
    CoreAlgorithmID algorithm{CoreAlgorithmID::None};
    float estimated_ratio{1.0f};
    float confidence{0.0f};
    std::string reason;
    std::string file_type;
    float shannon_entropy{0.0f};
    double extraction_time_ms{0.0};
};

class ADEBridge {
public:
    ADEBridge();

    auto set_mode(AlgorithmDecisionEngine::Mode mode) -> void;
    auto get_mode() const -> AlgorithmDecisionEngine::Mode;

    auto train(const std::vector<TrainingSample>& samples,
               const RandomForestConfig& config = {}) -> void;

    auto is_ml_ready() const -> bool;

    auto try_load_default_model() -> bool;
    auto load_model(const std::string& filepath) -> bool;
    auto save_model(const std::string& filepath) const -> bool;

    auto analyze(const uint8_t* data, size_t size) -> ADEBridgeResult;
    auto analyze(const std::vector<uint8_t>& data) -> ADEBridgeResult;
    auto analyze_file(const std::string& filepath) -> ADEBridgeResult;

    auto get_feature_importance() const -> std::vector<float>;
    auto to_json() const -> std::string;

    static auto map_algorithm(AlgorithmID ade_algo) -> CoreAlgorithmID;
    static auto map_algorithm(CoreAlgorithmID core_algo) -> AlgorithmID;
    static auto algorithm_id_to_string(CoreAlgorithmID id) -> std::string;

private:
    AlgorithmDecisionEngine engine_;
    FeatureExtractorV3 extractor_;
    AlgorithmDecisionEngine::Mode mode_;

    static auto get_executable_dir() -> std::string;
};

}  // namespace ade
}  // namespace compressor

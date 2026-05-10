#pragma once

#include "DecisionEngine.hpp"
#include "FeatureExtractorV3.hpp"

#include <cstdlib>
#include <fstream>
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
    ADEBridge() : mode_(AlgorithmDecisionEngine::Mode::RULE_BASED) {}

    auto set_mode(AlgorithmDecisionEngine::Mode mode) -> void {
        engine_.set_mode(mode);
        mode_ = mode;
    }

    auto get_mode() const -> AlgorithmDecisionEngine::Mode {
        return engine_.get_mode();
    }

    auto train(const std::vector<TrainingSample>& samples,
               const RandomForestConfig& config = {}) -> void {
        engine_.train(samples, config);
    }

    auto is_ml_ready() const -> bool {
        return engine_.is_ml_ready();
    }

    auto try_load_default_model() -> bool {
        std::vector<std::string> candidates;

        auto exe_dir = get_executable_dir();
        if (!exe_dir.empty()) {
            candidates.push_back(exe_dir + "/ade/default_model.bin");
            candidates.push_back(exe_dir + "/default_model.bin");
            candidates.push_back(exe_dir + "/../ade/default_model.bin");
            candidates.push_back(exe_dir + "/../default_model.bin");
        }

        const char* env_path = std::getenv("ADE_MODEL_PATH");
        if (env_path && env_path[0] != '\0') {
            candidates.insert(candidates.begin(), std::string(env_path));
        }

        candidates.push_back("ade/default_model.bin");
        candidates.push_back("default_model.bin");
        candidates.push_back("../ade/default_model.bin");
        candidates.push_back("../default_model.bin");
        candidates.push_back("../../ade/default_model.bin");
        candidates.push_back("../../default_model.bin");

        for (const auto& path : candidates) {
            if (engine_.load_model(path)) {
                return true;
            }
        }
        return false;
    }

    auto load_model(const std::string& filepath) -> bool {
        return engine_.load_model(filepath);
    }

    auto save_model(const std::string& filepath) const -> bool {
        return engine_.save_model(filepath);
    }

    auto analyze(const uint8_t* data, size_t size) -> ADEBridgeResult {
        ADEBridgeResult result;

        auto extraction = extractor_.extract(data, size);
        auto decision = engine_.decide(extraction.vector);

        result.algorithm = map_algorithm(decision.algorithm);
        result.estimated_ratio = decision.estimated_ratio;
        result.confidence = decision.confidence;
        result.reason = decision.reason;
        result.file_type = file_type_to_string(extraction.detection.type);
        result.shannon_entropy = extraction.vector.base.shannon_entropy;
        result.extraction_time_ms = extraction.extraction_time_ms;

        return result;
    }

    auto analyze(const std::vector<uint8_t>& data) -> ADEBridgeResult {
        if (data.empty()) {
            ADEBridgeResult result;
            result.algorithm = CoreAlgorithmID::None;
            result.reason = "Empty input";
            return result;
        }
        return analyze(data.data(), data.size());
    }

    auto analyze_file(const std::string& filepath) -> ADEBridgeResult {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file) {
            ADEBridgeResult result;
            result.algorithm = CoreAlgorithmID::None;
            result.reason = "File not found: " + filepath;
            return result;
        }

        auto file_size = file.tellg();
        if (file_size <= 0) {
            ADEBridgeResult result;
            result.algorithm = CoreAlgorithmID::None;
            result.reason = "Empty file";
            return result;
        }

        file.seekg(0, std::ios::beg);
        std::vector<uint8_t> buffer(static_cast<size_t>(file_size));
        file.read(reinterpret_cast<char*>(buffer.data()), static_cast<size_t>(file_size));
        file.close();

        return analyze(buffer);
    }

    auto get_feature_importance() const -> std::vector<float> {
        return engine_.feature_importance();
    }

    auto to_json() const -> std::string {
        return engine_.to_json();
    }

    static auto map_algorithm(AlgorithmID ade_algo) -> CoreAlgorithmID {
        switch (ade_algo) {
            case AlgorithmID::DEFLATE:
                return CoreAlgorithmID::Deflate;
            case AlgorithmID::LZSS:
                return CoreAlgorithmID::LZSS;
            case AlgorithmID::LZMINE:
                return CoreAlgorithmID::LZDP;
            case AlgorithmID::DPFLATE:
                return CoreAlgorithmID::DPFlate;
            case AlgorithmID::GZIP:
                return CoreAlgorithmID::Deflate;
            case AlgorithmID::BROTLI:
                return CoreAlgorithmID::Brotli;
            case AlgorithmID::ZSTD:
                return CoreAlgorithmID::Zstd;
            case AlgorithmID::SKIP:
            case AlgorithmID::NONE:
            default:
                return CoreAlgorithmID::None;
        }
    }

    static auto map_algorithm(CoreAlgorithmID core_algo) -> AlgorithmID {
        switch (core_algo) {
            case CoreAlgorithmID::Deflate:
                return AlgorithmID::DEFLATE;
            case CoreAlgorithmID::LZSS:
                return AlgorithmID::LZSS;
            case CoreAlgorithmID::LZDP:
                return AlgorithmID::LZMINE;
            case CoreAlgorithmID::DPFlate:
                return AlgorithmID::DPFLATE;
            case CoreAlgorithmID::Brotli:
                return AlgorithmID::BROTLI;
            case CoreAlgorithmID::Zstd:
                return AlgorithmID::ZSTD;
            default:
                return AlgorithmID::NONE;
        }
    }

    static auto algorithm_id_to_string(CoreAlgorithmID id) -> std::string {
        switch (id) {
            case CoreAlgorithmID::Deflate:   return "Deflate";
            case CoreAlgorithmID::Inflate:   return "Inflate";
            case CoreAlgorithmID::LZSS:      return "LZSS";
            case CoreAlgorithmID::LZDP:    return "LZDP";
            case CoreAlgorithmID::DPFlate:   return "DPFlate";
            case CoreAlgorithmID::Brotli:    return "Brotli";
            case CoreAlgorithmID::Zstd:      return "Zstd";
            case CoreAlgorithmID::None:      return "None";
            default:                         return "Unknown";
        }
    }

private:
    AlgorithmDecisionEngine engine_;
    FeatureExtractorV3 extractor_;
    AlgorithmDecisionEngine::Mode mode_;

    static auto get_executable_dir() -> std::string {
#ifdef _WIN32
        char path[MAX_PATH] = {0};
        DWORD len = GetModuleFileNameA(NULL, path, MAX_PATH);
        if (len == 0 || len == MAX_PATH) return "";
        std::string sp(path, len);
        auto pos = sp.find_last_of("\\/");
        if (pos == std::string::npos) return "";
        return sp.substr(0, pos);
#else
        return "";
#endif
    }
};

}  // namespace ade
}  // namespace compressor

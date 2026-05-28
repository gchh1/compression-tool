#include "ADEBridge.hpp"
#include "ade_debug_log.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace compressor {
namespace ade {

ADEBridge::ADEBridge() : mode_(AlgorithmDecisionEngine::Mode::RULE_BASED) {}

auto ADEBridge::set_mode(AlgorithmDecisionEngine::Mode mode) -> void {
    engine_.set_mode(mode);
    mode_ = mode;
}

auto ADEBridge::get_mode() const -> AlgorithmDecisionEngine::Mode {
    return engine_.get_mode();
}

auto ADEBridge::train(const std::vector<TrainingSample>& samples,
                      const RandomForestConfig& config) -> void {
    engine_.train(samples, config);
}

auto ADEBridge::is_ml_ready() const -> bool {
    return engine_.is_ml_ready();
}

auto ADEBridge::try_load_default_model() -> bool {
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

auto ADEBridge::load_model(const std::string& filepath) -> bool {
    return engine_.load_model(filepath);
}

auto ADEBridge::save_model(const std::string& filepath) const -> bool {
    return engine_.save_model(filepath);
}

auto ADEBridge::predict_padded(const std::vector<float>& padded_features) const -> int {
    return engine_.predict_padded(padded_features);
}

auto ADEBridge::analyze(const uint8_t* data, size_t size) -> ADEBridgeResult {
    char msg[128];
    std::snprintf(msg, sizeof(msg), "analyze: ENTER size=%zu", size);
    ade_debug_write(msg);
    ADEBridgeResult result;

    ade_debug_write("analyze: calling extractor_.extract");
    auto extraction = extractor_.extract(data, size);
    std::snprintf(msg, sizeof(msg), "analyze: extract returned, shannon_entropy=%.3f",
            extraction.vector.base.shannon_entropy);
    ade_debug_write(msg);

    ade_debug_write("analyze: calling engine_.decide");
    auto decision = engine_.decide(extraction.vector);
    std::snprintf(msg, sizeof(msg), "analyze: engine_.decide returned algo=%d",
            static_cast<int>(decision.algorithm));
    ade_debug_write(msg);

    result.algorithm = map_algorithm(decision.algorithm);
    result.estimated_ratio = decision.estimated_ratio;
    result.confidence = decision.confidence;
    result.reason = decision.reason;
    result.file_type = file_type_to_string(extraction.detection.type);
    result.shannon_entropy = extraction.vector.base.shannon_entropy;
    result.extraction_time_ms = extraction.extraction_time_ms;

    return result;
}

auto ADEBridge::analyze(const std::vector<uint8_t>& data) -> ADEBridgeResult {
    if (data.empty()) {
        ADEBridgeResult result;
        result.algorithm = CoreAlgorithmID::None;
        result.reason = "Empty input";
        return result;
    }
    return analyze(data.data(), data.size());
}

auto ADEBridge::analyze_file(const std::string& filepath) -> ADEBridgeResult {
    char msg[256];
    std::snprintf(msg, sizeof(msg), "analyze_file: ENTER path=%s", filepath.c_str());
    ade_debug_write(msg);
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) {
        ADEBridgeResult result;
        result.algorithm = CoreAlgorithmID::None;
        result.reason = "File not found: " + filepath;
        ade_debug_write("analyze_file: file not found, returning");
        return result;
    }

    auto file_size = file.tellg();
    if (file_size <= 0) {
        ADEBridgeResult result;
        result.algorithm = CoreAlgorithmID::None;
        result.reason = "Empty file";
        ade_debug_write("analyze_file: empty file, returning");
        return result;
    }

    std::snprintf(msg, sizeof(msg), "analyze_file: reading %lld bytes",
            static_cast<long long>(file_size));
    ade_debug_write(msg);
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(file_size));
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<size_t>(file_size));
    file.close();

    ade_debug_write("analyze_file: calling analyze(buffer)");
    return analyze(buffer);
}

auto ADEBridge::get_feature_importance() const -> std::vector<float> {
    return engine_.feature_importance();
}

auto ADEBridge::to_json() const -> std::string {
    return engine_.to_json();
}

auto ADEBridge::map_algorithm(AlgorithmID ade_algo) -> CoreAlgorithmID {
    switch (ade_algo) {
        case AlgorithmID::DEFLATE:
            return CoreAlgorithmID::Deflate;
        case AlgorithmID::LZSS:
            return CoreAlgorithmID::LZSS;
        case AlgorithmID::LZDP:
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

auto ADEBridge::map_algorithm(CoreAlgorithmID core_algo) -> AlgorithmID {
    switch (core_algo) {
        case CoreAlgorithmID::Deflate:
            return AlgorithmID::DEFLATE;
        case CoreAlgorithmID::LZSS:
            return AlgorithmID::LZSS;
        case CoreAlgorithmID::LZDP:
            return AlgorithmID::LZDP;
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

auto ADEBridge::algorithm_id_to_string(CoreAlgorithmID id) -> std::string {
    switch (id) {
        case CoreAlgorithmID::Deflate:
            return "Deflate";
        case CoreAlgorithmID::Inflate:
            return "Inflate";
        case CoreAlgorithmID::LZSS:
            return "LZSS";
        case CoreAlgorithmID::LZDP:
            return "LZDP";
        case CoreAlgorithmID::DPFlate:
            return "DPFlate";
        case CoreAlgorithmID::Brotli:
            return "Brotli";
        case CoreAlgorithmID::Zstd:
            return "Zstd";
        case CoreAlgorithmID::None:
            return "None";
        default:
            return "Unknown";
    }
}

auto ADEBridge::get_executable_dir() -> std::string {
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

}  // namespace ade
}  // namespace compressor

#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

namespace compressor {
namespace ade {

/// Stage2a: small fully-connected net (39 -> 128 -> 64 -> 32 -> 5), pure C++ matmul.
struct ParamRegressionRow {
    std::vector<float> features;  ///< length 39 (33 padded + 6 algo one-hot)
    std::vector<float> targets;   ///< length 5, normalized params in [0, 1]
    float weight{1.0f};
};

struct ParamRegressorTrainConfig {
    size_t epochs{50};
    float learning_rate{0.001f};
    size_t batch_size{32};
    float validation_split{0.2f};
    size_t random_seed{42};
};

struct ParamRegressorTrainMetrics {
    float final_train_loss{0.0f};
    float final_val_loss{0.0f};
    size_t epochs_completed{0};
    size_t num_samples{0};
};

class ParamRegressorNet {
public:
    static constexpr size_t kDefaultInputDim = 39;
    static constexpr size_t kDefaultOutputDim = 5;
    static constexpr size_t kDefaultHiddenDim = 128;

    ParamRegressorNet();

    auto is_trained() const -> bool { return trained_; }

    auto input_dim() const -> size_t { return input_dim_; }
    auto output_dim() const -> size_t { return output_dim_; }

    auto predict(const std::vector<float>& features) const -> std::vector<float>;

    auto train(const std::vector<ParamRegressionRow>& rows,
               const ParamRegressorTrainConfig& config = {}) -> ParamRegressorTrainMetrics;

    auto save(const std::string& filepath) const -> bool;
    auto load(const std::string& filepath) -> bool;

    auto try_load_default() -> bool;

private:
    size_t input_dim_{kDefaultInputDim};
    size_t hidden_dim_{kDefaultHiddenDim};
    size_t output_dim_{kDefaultOutputDim};
    bool trained_{false};

    // Layer 1: hidden x input
    std::vector<float> w1_;
    std::vector<float> b1_;
    // Layer 2: hidden/2 x hidden
    std::vector<float> w2_;
    std::vector<float> b2_;
    // Layer 3: 32 x hidden/2
    std::vector<float> w3_;
    std::vector<float> b3_;
    // Layer 4: output x 32
    std::vector<float> w4_;
    std::vector<float> b4_;

    auto init_random_weights(uint64_t seed) -> void;
    auto forward(const std::vector<float>& x, std::vector<float>& h1, std::vector<float>& h2,
                 std::vector<float>& h3, std::vector<float>& out) const -> void;

    static auto relu(float v) -> float;
    static auto relu_deriv(float v) -> float;
};

}  // namespace ade
}  // namespace compressor

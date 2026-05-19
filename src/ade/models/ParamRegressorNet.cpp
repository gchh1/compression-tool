#include "ParamRegressorNet.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#ifdef _WIN32
#include <windows.h>
#endif
#include <limits>
#include <numeric>
#include <random>
#include <sstream>

namespace compressor {
namespace ade {

namespace {

constexpr char kMagic[4] = {'P', 'R', 'G', '3'};
constexpr uint32_t kFileVersion = 1;

auto sigmoid(float x) -> float {
    if (x >= 0.0f) {
        const float z = std::exp(-x);
        return 1.0f / (1.0f + z);
    }
    const float z = std::exp(x);
    return z / (1.0f + z);
}

auto matvec(const std::vector<float>& w, size_t out_dim, size_t in_dim,
            const std::vector<float>& x, const std::vector<float>& b, std::vector<float>& y) -> void {
    y.assign(out_dim, 0.0f);
    for (size_t o = 0; o < out_dim; ++o) {
        float sum = b[o];
        const size_t row = o * in_dim;
        for (size_t i = 0; i < in_dim; ++i) {
            sum += w[row + i] * x[i];
        }
        y[o] = sum;
    }
}

}  // namespace

ParamRegressorNet::ParamRegressorNet() {
    init_random_weights(42);
}

auto ParamRegressorNet::relu(float v) -> float {
    return v > 0.0f ? v : 0.0f;
}

auto ParamRegressorNet::relu_deriv(float v) -> float {
    return v > 0.0f ? 1.0f : 0.0f;
}

auto ParamRegressorNet::init_random_weights(uint64_t seed) -> void {
    std::mt19937 rng(static_cast<uint32_t>(seed));
    std::normal_distribution<float> dist(0.0f, 0.05f);

    const size_t h2 = hidden_dim_ / 2;
    const size_t h3 = 32;

    w1_.resize(hidden_dim_ * input_dim_);
    b1_.resize(hidden_dim_);
    w2_.resize(h2 * hidden_dim_);
    b2_.resize(h2);
    w3_.resize(h3 * h2);
    b3_.resize(h3);
    w4_.resize(output_dim_ * h3);
    b4_.resize(output_dim_);

    auto fill = [&](std::vector<float>& v) {
        for (auto& x : v) {
            x = dist(rng);
        }
    };
    fill(w1_);
    fill(w2_);
    fill(w3_);
    fill(w4_);
    std::fill(b1_.begin(), b1_.end(), 0.0f);
    std::fill(b2_.begin(), b2_.end(), 0.0f);
    std::fill(b3_.begin(), b3_.end(), 0.0f);
    std::fill(b4_.begin(), b4_.end(), 0.0f);
    trained_ = false;
}

auto ParamRegressorNet::forward(const std::vector<float>& x, std::vector<float>& h1,
                                std::vector<float>& h2, std::vector<float>& h3,
                                std::vector<float>& out) const -> void {
    const size_t h2d = hidden_dim_ / 2;
    const size_t h3d = 32;

    matvec(w1_, hidden_dim_, input_dim_, x, b1_, h1);
    for (auto& v : h1) {
        v = relu(v);
    }

    matvec(w2_, h2d, hidden_dim_, h1, b2_, h2);
    for (auto& v : h2) {
        v = relu(v);
    }

    matvec(w3_, h3d, h2d, h2, b3_, h3);
    for (auto& v : h3) {
        v = relu(v);
    }

    matvec(w4_, output_dim_, h3d, h3, b4_, out);
    for (auto& v : out) {
        v = sigmoid(v);
    }
}

auto ParamRegressorNet::predict(const std::vector<float>& features) const -> std::vector<float> {
    if (!trained_ || features.size() < input_dim_) {
        return std::vector<float>(output_dim_, 0.5f);
    }
    std::vector<float> x(input_dim_, 0.0f);
    for (size_t i = 0; i < input_dim_; ++i) {
        x[i] = features[i];
    }
    std::vector<float> h1, h2, h3, out;
    forward(x, h1, h2, h3, out);
    return out;
}

auto ParamRegressorNet::train(const std::vector<ParamRegressionRow>& rows,
                              const ParamRegressorTrainConfig& config) -> ParamRegressorTrainMetrics {
    ParamRegressorTrainMetrics metrics{};
    if (rows.size() < 4) {
        return metrics;
    }

    init_random_weights(config.random_seed);

    std::vector<ParamRegressionRow> data = rows;
    std::mt19937 rng(static_cast<uint32_t>(config.random_seed));
    std::shuffle(data.begin(), data.end(), rng);

    // Small sets: train on all rows (no holdout). Never use train_end > data.size().
    size_t train_end = data.size();
    size_t val_begin = data.size();
    if (data.size() >= 8) {
        train_end = static_cast<size_t>(
            static_cast<double>(data.size()) * (1.0 - config.validation_split));
        if (train_end < 1) {
            train_end = 1;
        }
        if (train_end >= data.size()) {
            train_end = data.size() - 1;
        }
        val_begin = train_end;
    }

    const size_t h2d = hidden_dim_ / 2;
    const size_t h3d = 32;
    const float lr = config.learning_rate;

    auto run_epoch = [&](size_t begin, size_t end, bool update) -> float {
        float total_loss = 0.0f;
        size_t count = 0;

        for (size_t idx = begin; idx < end;) {
            const size_t batch_end = std::min(end, idx + config.batch_size);
            std::vector<float> gw1(w1_.size(), 0.0f), gb1(b1_.size(), 0.0f);
            std::vector<float> gw2(w2_.size(), 0.0f), gb2(b2_.size(), 0.0f);
            std::vector<float> gw3(w3_.size(), 0.0f), gb3(b3_.size(), 0.0f);
            std::vector<float> gw4(w4_.size(), 0.0f), gb4(b4_.size(), 0.0f);
            size_t batch_n = 0;

            for (size_t bi = idx; bi < batch_end; ++bi) {
                const auto& row = data[bi];
                if (row.features.size() < input_dim_ || row.targets.size() < output_dim_) {
                    continue;
                }

                std::vector<float> x(input_dim_, 0.0f);
                for (size_t i = 0; i < input_dim_; ++i) {
                    x[i] = row.features[i];
                }

                std::vector<float> z1, h1, z2, h2, z3, h3, z4, out;
                z1.resize(hidden_dim_);
                h1.resize(hidden_dim_);
                matvec(w1_, hidden_dim_, input_dim_, x, b1_, z1);
                for (size_t i = 0; i < hidden_dim_; ++i) {
                    h1[i] = relu(z1[i]);
                }

                z2.resize(h2d);
                h2.resize(h2d);
                matvec(w2_, h2d, hidden_dim_, h1, b2_, z2);
                for (size_t i = 0; i < h2d; ++i) {
                    h2[i] = relu(z2[i]);
                }

                z3.resize(h3d);
                h3.resize(h3d);
                matvec(w3_, h3d, h2d, h2, b3_, z3);
                for (size_t i = 0; i < h3d; ++i) {
                    h3[i] = relu(z3[i]);
                }

                z4.resize(output_dim_);
                out.resize(output_dim_);
                matvec(w4_, output_dim_, h3d, h3, b4_, z4);
                for (size_t i = 0; i < output_dim_; ++i) {
                    out[i] = sigmoid(z4[i]);
                }

                float loss = 0.0f;
                std::vector<float> d_out(output_dim_);
                for (size_t i = 0; i < output_dim_; ++i) {
                    const float err = out[i] - row.targets[i];
                    loss += err * err;
                    d_out[i] = 2.0f * err * out[i] * (1.0f - out[i]) * row.weight;
                }
                loss *= row.weight / static_cast<float>(output_dim_);
                total_loss += loss;
                ++count;
                ++batch_n;

                std::vector<float> d_h3(h3d);
                for (size_t o = 0; o < output_dim_; ++o) {
                    gb4[o] += d_out[o];
                    for (size_t i = 0; i < h3d; ++i) {
                        gw4[o * h3d + i] += d_out[o] * h3[i];
                        d_h3[i] += w4_[o * h3d + i] * d_out[o];
                    }
                }

                std::vector<float> d_z3(h3d);
                for (size_t i = 0; i < h3d; ++i) {
                    d_z3[i] = d_h3[i] * relu_deriv(z3[i]);
                }

                std::vector<float> d_h2(h2d);
                for (size_t o = 0; o < h3d; ++o) {
                    gb3[o] += d_z3[o];
                    for (size_t i = 0; i < h2d; ++i) {
                        gw3[o * h2d + i] += d_z3[o] * h2[i];
                        d_h2[i] += w3_[o * h2d + i] * d_z3[o];
                    }
                }

                std::vector<float> d_z2(h2d);
                for (size_t i = 0; i < h2d; ++i) {
                    d_z2[i] = d_h2[i] * relu_deriv(z2[i]);
                }

                std::vector<float> d_h1(hidden_dim_);
                for (size_t o = 0; o < h2d; ++o) {
                    gb2[o] += d_z2[o];
                    for (size_t i = 0; i < hidden_dim_; ++i) {
                        gw2[o * hidden_dim_ + i] += d_z2[o] * h1[i];
                        d_h1[i] += w2_[o * hidden_dim_ + i] * d_z2[o];
                    }
                }

                std::vector<float> d_z1(hidden_dim_);
                for (size_t i = 0; i < hidden_dim_; ++i) {
                    d_z1[i] = d_h1[i] * relu_deriv(z1[i]);
                }

                for (size_t o = 0; o < hidden_dim_; ++o) {
                    gb1[o] += d_z1[o];
                    for (size_t i = 0; i < input_dim_; ++i) {
                        gw1[o * input_dim_ + i] += d_z1[o] * x[i];
                    }
                }
            }

            if (update && batch_n > 0) {
                const float scale = lr / static_cast<float>(batch_n);
                auto apply = [&](std::vector<float>& w, const std::vector<float>& g) {
                    for (size_t i = 0; i < w.size(); ++i) {
                        w[i] -= scale * g[i];
                    }
                };
                apply(w1_, gw1);
                apply(b1_, gb1);
                apply(w2_, gw2);
                apply(b2_, gb2);
                apply(w3_, gw3);
                apply(b3_, gb3);
                apply(w4_, gw4);
                apply(b4_, gb4);
            }

            idx = batch_end;
        }

        return count > 0 ? total_loss / static_cast<float>(count) : 0.0f;
    };

    float best_val = std::numeric_limits<float>::max();
    size_t patience = 0;
    const size_t patience_limit = 10;

    for (size_t epoch = 0; epoch < config.epochs; ++epoch) {
        const float train_loss = run_epoch(0, train_end, true);
        const float val_loss =
            val_begin < data.size() ? run_epoch(val_begin, data.size(), false) : train_loss;

        metrics.final_train_loss = train_loss;
        metrics.final_val_loss = val_loss;
        metrics.epochs_completed = epoch + 1;
        metrics.num_samples = data.size();

        if (val_loss < best_val - 1e-6f) {
            best_val = val_loss;
            patience = 0;
        } else {
            ++patience;
        }
        if (patience >= patience_limit) {
            break;
        }
    }

    trained_ = true;
    return metrics;
}

auto ParamRegressorNet::save(const std::string& filepath) const -> bool {
    if (!trained_) {
        return false;
    }
    std::ofstream os(filepath, std::ios::binary);
    if (!os) {
        return false;
    }
    os.write(kMagic, 4);
    const uint32_t ver = kFileVersion;
    os.write(reinterpret_cast<const char*>(&ver), sizeof(ver));
    auto write_u32 = [&](uint32_t v) { os.write(reinterpret_cast<const char*>(&v), sizeof(v)); };
    write_u32(static_cast<uint32_t>(input_dim_));
    write_u32(static_cast<uint32_t>(hidden_dim_));
    write_u32(static_cast<uint32_t>(output_dim_));

    auto write_vec = [&](const std::vector<float>& v) {
        const uint32_t n = static_cast<uint32_t>(v.size());
        os.write(reinterpret_cast<const char*>(&n), sizeof(n));
        os.write(reinterpret_cast<const char*>(v.data()),
                 static_cast<std::streamsize>(v.size() * sizeof(float)));
    };
    write_vec(w1_);
    write_vec(b1_);
    write_vec(w2_);
    write_vec(b2_);
    write_vec(w3_);
    write_vec(b3_);
    write_vec(w4_);
    write_vec(b4_);
    return static_cast<bool>(os);
}

auto ParamRegressorNet::load(const std::string& filepath) -> bool {
    std::ifstream is(filepath, std::ios::binary);
    if (!is) {
        return false;
    }
    char magic[4];
    is.read(magic, 4);
    if (std::memcmp(magic, kMagic, 4) != 0) {
        return false;
    }
    uint32_t ver = 0;
    is.read(reinterpret_cast<char*>(&ver), sizeof(ver));
    if (ver != kFileVersion) {
        return false;
    }
    uint32_t in_d = 0, hid = 0, out_d = 0;
    is.read(reinterpret_cast<char*>(&in_d), sizeof(in_d));
    is.read(reinterpret_cast<char*>(&hid), sizeof(hid));
    is.read(reinterpret_cast<char*>(&out_d), sizeof(out_d));
    input_dim_ = in_d;
    hidden_dim_ = hid;
    output_dim_ = out_d;

    auto read_vec = [&](std::vector<float>& v) -> bool {
        uint32_t n = 0;
        is.read(reinterpret_cast<char*>(&n), sizeof(n));
        v.resize(n);
        is.read(reinterpret_cast<char*>(v.data()),
                static_cast<std::streamsize>(n * sizeof(float)));
        return static_cast<bool>(is);
    };
    if (!read_vec(w1_) || !read_vec(b1_) || !read_vec(w2_) || !read_vec(b2_) || !read_vec(w3_) ||
        !read_vec(b3_) || !read_vec(w4_) || !read_vec(b4_)) {
        return false;
    }
    trained_ = true;
    return true;
}

auto ParamRegressorNet::try_load_default() -> bool {
    std::vector<std::string> candidates;
#ifdef _WIN32
    char path[MAX_PATH] = {0};
    if (GetModuleFileNameA(nullptr, path, MAX_PATH) != 0) {
        std::string sp(path);
        const auto pos = sp.find_last_of("\\/");
        if (pos != std::string::npos) {
            const std::string exe_dir = sp.substr(0, pos);
            candidates.push_back(exe_dir + "/ade/param_regressor.bin");
            candidates.push_back(exe_dir + "/param_regressor.bin");
            candidates.push_back(exe_dir + "/../ade/param_regressor.bin");
        }
    }
#endif
    candidates.push_back("ade/param_regressor.bin");
    candidates.push_back("param_regressor.bin");
    candidates.push_back("../ade/param_regressor.bin");
    candidates.push_back("../../ade/param_regressor.bin");
    candidates.push_back("Package/ade/param_regressor.bin");
    for (const auto& path : candidates) {
        if (load(path)) {
            return true;
        }
    }
    return false;
}

}  // namespace ade
}  // namespace compressor

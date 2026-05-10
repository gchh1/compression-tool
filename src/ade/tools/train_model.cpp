#include "DecisionEngine.hpp"
#include "FeatureVectorV3.hpp"
#include "RandomForest.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace compressor::ade;

static auto extract_int_from_obj(const std::string& obj, size_t pos) -> int;

static auto load_training_data(const std::string& filepath)
    -> std::vector<TrainingSample> {
    std::vector<TrainingSample> samples;

    std::ifstream file(filepath);
    if (!file) {
        std::cerr << "ERROR: Cannot open " << filepath << "\n";
        return samples;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    file.close();

    auto find_key = [&content](const std::string& key) -> size_t {
        std::string search = "\"" + key + "\"";
        return content.find(search);
    };

    auto extract_int = [&content](size_t pos) -> int {
        pos = content.find(':', pos);
        if (pos == std::string::npos) return 0;
        pos++;
        while (pos < content.size() && (content[pos] == ' ' || content[pos] == '\t')) pos++;
        std::string num;
        while (pos < content.size() && (std::isdigit(content[pos]) || content[pos] == '-')) {
            num += content[pos];
            pos++;
        }
        return num.empty() ? 0 : std::stoi(num);
    };

    size_t samples_start = content.find("\"samples\"");
    if (samples_start == std::string::npos) {
        std::cerr << "ERROR: No 'samples' key found\n";
        return samples;
    }

    samples_start = content.find('[', samples_start);
    if (samples_start == std::string::npos) {
        std::cerr << "ERROR: No samples array found\n";
        return samples;
    }

    size_t pos = samples_start + 1;
    size_t depth = 1;
    size_t obj_start = std::string::npos;

    while (pos < content.size() && depth > 0) {
        if (content[pos] == '{' && depth == 1) {
            obj_start = pos;
        } else if (content[pos] == '}' && depth == 1 && obj_start != std::string::npos) {
            std::string obj = content.substr(obj_start, pos - obj_start + 1);

            TrainingSample sample;

            size_t feat_start = obj.find("\"features\"");
            if (feat_start != std::string::npos) {
                feat_start = obj.find('[', feat_start);
                size_t feat_end = obj.find(']', feat_start);
                if (feat_start != std::string::npos && feat_end != std::string::npos) {
                    std::string feat_str = obj.substr(feat_start + 1, feat_end - feat_start - 1);
                    std::istringstream feat_stream(feat_str);
                    std::string token;
                    while (std::getline(feat_stream, token, ',')) {
                        while (!token.empty() && (token[0] == ' ' || token[0] == '\t')) {
                            token.erase(0, 1);
                        }
                        if (!token.empty()) {
                            sample.features.push_back(std::stof(token));
                        }
                    }
                }
            }

            size_t label_pos = obj.find("\"label\"");
            if (label_pos != std::string::npos) {
                sample.label = extract_int_from_obj(obj, label_pos);
            }

            sample.weight = 1.0f;
            samples.push_back(std::move(sample));

            obj_start = std::string::npos;
        } else if (content[pos] == '{') {
            depth++;
        } else if (content[pos] == '}') {
            depth--;
        }
        pos++;
    }

    return samples;
}

static auto extract_int_from_obj(const std::string& obj, size_t pos) -> int {
    pos = obj.find(':', pos);
    if (pos == std::string::npos) return 0;
    pos++;
    while (pos < obj.size() && (obj[pos] == ' ' || obj[pos] == '\t')) pos++;
    std::string num;
    while (pos < obj.size() && (std::isdigit(obj[pos]) || obj[pos] == '-')) {
        num += obj[pos];
        pos++;
    }
    return num.empty() ? 0 : std::stoi(num);
}

static auto print_class_distribution(const std::vector<TrainingSample>& samples) -> void {
    std::vector<size_t> counts(10, 0);
    for (const auto& s : samples) {
        if (s.label >= 0 && static_cast<size_t>(s.label) < counts.size()) {
            counts[s.label]++;
        }
    }

    std::cout << "\nClass Distribution:\n";
    std::cout << "  " << std::left << std::setw(20) << "Class" << std::setw(10) << "Count"
              << "Percentage\n";
    std::cout << "  " << std::string(40, '-') << "\n";

    const char* names[] = {"NONE",    "DEFLATE", "LZSS",    "LZMINE", "DPFLATE",
                           "GZIP",    "DELTA",   "BROTLI",  "ZSTD",   "SKIP"};

    for (size_t i = 0; i < counts.size(); ++i) {
        if (counts[i] > 0) {
            float pct = 100.0f * static_cast<float>(counts[i]) /
                        static_cast<float>(samples.size());
            std::cout << "  " << std::left << std::setw(20) << names[i] << std::setw(10)
                      << counts[i] << std::fixed << std::setprecision(1) << pct << "%\n";
        }
    }
    std::cout << "  " << std::string(40, '-') << "\n";
    std::cout << "  " << std::left << std::setw(20) << "TOTAL" << std::setw(10)
              << samples.size() << "100.0%\n\n";
}

auto main(int argc, char* argv[]) -> int {
    std::string data_path = "src/ade/data/training_data_v3.json";
    std::string output_path = "src/ade/data/default_model.bin";

    if (argc > 1) data_path = argv[1];
    if (argc > 2) output_path = argv[2];

    std::cout << "=== ADE RandomForest Training Tool ===\n\n";
    std::cout << "Data:   " << data_path << "\n";
    std::cout << "Output: " << output_path << "\n\n";

    std::cout << "Loading training data...\n";
    auto samples = load_training_data(data_path);

    if (samples.empty()) {
        std::cerr << "ERROR: No training samples loaded\n";
        return 1;
    }

    std::cout << "Loaded " << samples.size() << " samples\n";
    std::cout << "Feature dimension: " << (samples[0].features.size()) << "\n";

    print_class_distribution(samples);

    RandomForestConfig config;
    config.num_trees = 100;
    config.max_depth = 12;
    config.min_samples_split = 5;
    config.min_samples_leaf = 2;
    config.max_features = 0;
    config.random_seed = 42;
    config.bootstrap = true;
    config.bootstrap_ratio = 0.8;

    std::cout << "Training configuration:\n";
    std::cout << "  Trees:              " << config.num_trees << "\n";
    std::cout << "  Max Depth:          " << config.max_depth << "\n";
    std::cout << "  Min Samples Split:  " << config.min_samples_split << "\n";
    std::cout << "  Min Samples Leaf:   " << config.min_samples_leaf << "\n";
    std::cout << "  Max Features:       "
              << (config.max_features == 0 ? "sqrt(n)" : std::to_string(config.max_features))
              << "\n";
    std::cout << "  Bootstrap Ratio:    " << config.bootstrap_ratio << "\n\n";

    std::cout << "Training RandomForest...\n";
    auto t_start = std::chrono::high_resolution_clock::now();

    RandomForest forest;
    forest.train(samples, config);

    auto t_end = std::chrono::high_resolution_clock::now();
    auto train_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(t_end - t_start).count();

    std::cout << "Training complete in " << train_ms << " ms\n";
    std::cout << "Trees trained: " << forest.num_trees() << "\n\n";

    std::cout << "Feature Importance:\n";
    auto importance = forest.feature_importance();
    const char* feat_names[] = {
        "shannon_entropy",   "unique_byte_ratio",  "printable_ratio",
        "rle_potential",     "dict_potential",     "skewness",
        "unique_bigram",     "file_size_log2",     "zero_byte_ratio",
        "local_entropy_var", "markov_entropy",     "entropy_skew",
        "byte_freq_var",     "run_length_max",     "entropy_trend",
        "file_size_raw",     "bigram_entropy",     "trigram_ratio",
        "ascii_ratio",       "null_byte_ratio",    "ext_type",
        "ext_subtype",       "ext_confidence",     "ext_feature_0",
        "ext_feature_1",     "ext_feature_2",      "ext_feature_3",
        "ext_feature_4",     "ext_feature_5",      "ext_feature_6",
        "ext_feature_7",     "ext_feature_8",      "ext_feature_9"};

    for (size_t i = 0; i < importance.size() && i < 33; ++i) {
        if (importance[i] > 0.001f) {
            std::cout << "  [" << std::setw(2) << i << "] " << std::left << std::setw(20)
                      << feat_names[i] << std::fixed << std::setprecision(4) << importance[i]
                      << "\n";
        }
    }
    std::cout << "\n";

    std::cout << "Saving model to " << output_path << "...\n";
    if (forest.save(output_path)) {
        std::cout << "Model saved successfully!\n";
    } else {
        std::cerr << "ERROR: Failed to save model\n";
        return 1;
    }

    std::cout << "\nVerifying model...\n";
    RandomForest loaded;
    if (loaded.load(output_path)) {
        std::cout << "Model loaded back successfully (" << loaded.num_trees()
                  << " trees)\n";

        size_t correct = 0;
        for (const auto& s : samples) {
            int pred = loaded.predict(s.features);
            if (pred == s.label) correct++;
        }
        float accuracy = 100.0f * static_cast<float>(correct) /
                         static_cast<float>(samples.size());
        std::cout << "Training accuracy: " << std::fixed << std::setprecision(2) << accuracy
                  << "% (" << correct << "/" << samples.size() << ")\n";
    } else {
        std::cerr << "WARNING: Model verification failed\n";
    }

    std::cout << "\nDone!\n";
    return 0;
}
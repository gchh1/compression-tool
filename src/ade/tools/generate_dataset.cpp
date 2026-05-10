#include "ADEBridge.hpp"
#include "DecisionEngine.hpp"
#include "FeatureExtractorV3.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <vector>

using namespace compressor::ade;

static auto operator<<(std::ostream& os, AlgorithmID id) -> std::ostream& {
    return os << algorithm_id_to_string(id);
}

class DatasetGenerator {
public:
    DatasetGenerator(uint32_t seed = 42)
        : rng_(seed), total_generated_(0) {}

    struct Sample {
        std::vector<float> features;
        int label;
        std::string label_name;
        std::string source;
    };

    auto generate_text_samples(size_t count) -> std::vector<Sample> {
        std::vector<Sample> samples;
        samples.reserve(count);

        std::uniform_int_distribution<int> type_dist(0, 4);

        for (size_t i = 0; i < count; ++i) {
            int subtype = type_dist(rng_);
            std::vector<uint8_t> data;

            switch (subtype) {
                case 0: data = gen_english_text(); break;
                case 1: data = gen_code_text(); break;
                case 2: data = gen_json_text(); break;
                case 3: data = gen_repetitive_text(); break;
                case 4: data = gen_mixed_text(); break;
            }

            auto extraction = extractor_.extract(data.data(), data.size());
            auto features = extraction.vector.to_padded_array();

            Sample s;
            s.features = features;
            s.label = static_cast<int>(AlgorithmID::DPFLATE);
            s.label_name = "DPFLATE";
            s.source = "text_subtype_" + std::to_string(subtype);
            samples.push_back(std::move(s));
        }

        total_generated_ += count;
        return samples;
    }

    auto generate_random_samples(size_t count) -> std::vector<Sample> {
        std::vector<Sample> samples;
        samples.reserve(count);

        std::uniform_int_distribution<int> type_dist(0, 3);

        for (size_t i = 0; i < count; ++i) {
            int subtype = type_dist(rng_);
            std::vector<uint8_t> data;

            switch (subtype) {
                case 0: data = gen_random_bytes(); break;
                case 1: data = gen_encrypted_like(); break;
                case 2: data = gen_already_compressed(); break;
                case 3: data = gen_uniform_distribution(); break;
            }

            auto extraction = extractor_.extract(data.data(), data.size());
            auto features = extraction.vector.to_padded_array();

            Sample s;
            s.features = features;
            s.label = static_cast<int>(AlgorithmID::SKIP);
            s.label_name = "SKIP";
            s.source = "random_subtype_" + std::to_string(subtype);
            samples.push_back(std::move(s));
        }

        total_generated_ += count;
        return samples;
    }

    auto generate_rle_samples(size_t count) -> std::vector<Sample> {
        std::vector<Sample> samples;
        samples.reserve(count);

        std::uniform_int_distribution<int> type_dist(0, 2);

        for (size_t i = 0; i < count; ++i) {
            int subtype = type_dist(rng_);
            std::vector<uint8_t> data;

            switch (subtype) {
                case 0: data = gen_run_length_data(); break;
                case 1: data = gen_sparse_data(); break;
                case 2: data = gen_bitmap_like(); break;
            }

            auto extraction = extractor_.extract(data.data(), data.size());
            auto features = extraction.vector.to_padded_array();

            Sample s;
            s.features = features;
            s.label = static_cast<int>(AlgorithmID::LZSS);
            s.label_name = "LZSS";
            s.source = "rle_subtype_" + std::to_string(subtype);
            samples.push_back(std::move(s));
        }

        total_generated_ += count;
        return samples;
    }

    auto generate_dict_samples(size_t count) -> std::vector<Sample> {
        std::vector<Sample> samples;
        samples.reserve(count);

        std::uniform_int_distribution<int> type_dist(0, 2);

        for (size_t i = 0; i < count; ++i) {
            int subtype = type_dist(rng_);
            std::vector<uint8_t> data;

            switch (subtype) {
                case 0: data = gen_dictionary_rich(); break;
                case 1: data = gen_log_data(); break;
                case 2: data = gen_csv_data(); break;
            }

            auto extraction = extractor_.extract(data.data(), data.size());
            auto features = extraction.vector.to_padded_array();

            Sample s;
            s.features = features;
            s.label = static_cast<int>(AlgorithmID::DEFLATE);
            s.label_name = "DEFLATE";
            s.source = "dict_subtype_" + std::to_string(subtype);
            samples.push_back(std::move(s));
        }

        total_generated_ += count;
        return samples;
    }

    auto generate_multimedia_samples(size_t count) -> std::vector<Sample> {
        std::vector<Sample> samples;
        samples.reserve(count);

        std::uniform_int_distribution<int> type_dist(0, 3);

        for (size_t i = 0; i < count; ++i) {
            int subtype = type_dist(rng_);
            std::vector<uint8_t> data;

            switch (subtype) {
                case 0: data = gen_bmp_image(); break;
                case 1: data = gen_wav_audio(); break;
                case 2: data = gen_png_like(); break;
                case 3: data = gen_jpeg_like(); break;
            }

            auto extraction = extractor_.extract(data.data(), data.size());
            auto features = extraction.vector.to_padded_array();

            Sample s;
            s.features = features;
            s.label = static_cast<int>(AlgorithmID::SKIP);
            s.label_name = "SKIP";
            s.source = "multimedia_subtype_" + std::to_string(subtype);
            samples.push_back(std::move(s));
        }

        total_generated_ += count;
        return samples;
    }

    auto generate_archive_samples(size_t count) -> std::vector<Sample> {
        std::vector<Sample> samples;
        samples.reserve(count);

        std::uniform_int_distribution<int> type_dist(0, 2);

        for (size_t i = 0; i < count; ++i) {
            int subtype = type_dist(rng_);
            std::vector<uint8_t> data;

            switch (subtype) {
                case 0: data = gen_zip_like(); break;
                case 1: data = gen_gzip_like(); break;
                case 2: data = gen_zlib_like(); break;
            }

            auto extraction = extractor_.extract(data.data(), data.size());
            auto features = extraction.vector.to_padded_array();

            Sample s;
            s.features = features;
            s.label = static_cast<int>(AlgorithmID::SKIP);
            s.label_name = "SKIP";
            s.source = "archive_subtype_" + std::to_string(subtype);
            samples.push_back(std::move(s));
        }

        total_generated_ += count;
        return samples;
    }

    auto generate_brotli_samples(size_t count) -> std::vector<Sample> {
        std::vector<Sample> samples;
        samples.reserve(count);

        std::uniform_int_distribution<int> type_dist(0, 2);

        for (size_t i = 0; i < count; ++i) {
            int subtype = type_dist(rng_);
            std::vector<uint8_t> data;

            switch (subtype) {
                case 0: data = gen_structured_text(); break;
                case 1: data = gen_html_like(); break;
                case 2: data = gen_xml_like(); break;
            }

            auto extraction = extractor_.extract(data.data(), data.size());
            auto features = extraction.vector.to_padded_array();

            Sample s;
            s.features = features;
            s.label = static_cast<int>(AlgorithmID::BROTLI);
            s.label_name = "BROTLI";
            s.source = "brotli_subtype_" + std::to_string(subtype);
            samples.push_back(std::move(s));
        }

        total_generated_ += count;
        return samples;
    }

    auto generate_zstd_samples(size_t count) -> std::vector<Sample> {
        std::vector<Sample> samples;
        samples.reserve(count);

        std::uniform_int_distribution<int> type_dist(0, 2);

        for (size_t i = 0; i < count; ++i) {
            int subtype = type_dist(rng_);
            std::vector<uint8_t> data;

            switch (subtype) {
                case 0: data = gen_mixed_binary(); break;
                case 1: data = gen_semi_structured(); break;
                case 2: data = gen_log_like(); break;
            }

            auto extraction = extractor_.extract(data.data(), data.size());
            auto features = extraction.vector.to_padded_array();

            Sample s;
            s.features = features;
            s.label = static_cast<int>(AlgorithmID::ZSTD);
            s.label_name = "ZSTD";
            s.source = "zstd_subtype_" + std::to_string(subtype);
            samples.push_back(std::move(s));
        }

        total_generated_ += count;
        return samples;
    }

    auto generate_all(size_t per_category = 100) -> std::vector<Sample> {
        std::vector<Sample> all;

        auto text = generate_text_samples(per_category);
        auto random = generate_random_samples(per_category);
        auto rle = generate_rle_samples(per_category);
        auto dict = generate_dict_samples(per_category);
        auto media = generate_multimedia_samples(per_category);
        auto archive = generate_archive_samples(per_category);
        auto brotli = generate_brotli_samples(per_category);
        auto zstd = generate_zstd_samples(per_category);

        all.insert(all.end(), text.begin(), text.end());
        all.insert(all.end(), random.begin(), random.end());
        all.insert(all.end(), rle.begin(), rle.end());
        all.insert(all.end(), dict.begin(), dict.end());
        all.insert(all.end(), media.begin(), media.end());
        all.insert(all.end(), archive.begin(), archive.end());
        all.insert(all.end(), brotli.begin(), brotli.end());
        all.insert(all.end(), zstd.begin(), zstd.end());

        std::shuffle(all.begin(), all.end(), rng_);
        return all;
    }

    auto total_generated() const -> size_t { return total_generated_; }

    static auto samples_to_json(const std::vector<Sample>& samples) -> std::string {
        std::ostringstream oss;
        oss << "{\n";
        oss << "  \"version\": \"3.0\",\n";
        oss << "  \"feature_count\": " << (samples.empty() ? 0 : samples[0].features.size()) << ",\n";
        oss << "  \"sample_count\": " << samples.size() << ",\n";
        oss << "  \"samples\": [\n";

        for (size_t i = 0; i < samples.size(); ++i) {
            const auto& s = samples[i];
            oss << "    {\"features\": [";
            for (size_t j = 0; j < s.features.size(); ++j) {
                if (j > 0) oss << ", ";
                oss << std::fixed << std::setprecision(6) << s.features[j];
            }
            oss << "], \"label\": " << s.label;
            oss << ", \"label_name\": \"" << s.label_name << "\"";
            oss << ", \"source\": \"" << s.source << "\"}";
            if (i + 1 < samples.size()) oss << ",";
            oss << "\n";
        }

        oss << "  ]\n";
        oss << "}\n";
        return oss.str();
    }

    static auto samples_to_csv(const std::vector<Sample>& samples) -> std::string {
        std::ostringstream oss;

        if (samples.empty()) return "";

        size_t n_features = samples[0].features.size();
        for (size_t j = 0; j < n_features; ++j) {
            oss << "f" << j << ",";
        }
        oss << "label,label_name,source\n";

        for (const auto& s : samples) {
            for (size_t j = 0; j < s.features.size(); ++j) {
                if (j > 0) oss << ",";
                oss << std::fixed << std::setprecision(6) << s.features[j];
            }
            oss << "," << s.label;
            oss << "," << s.label_name;
            oss << "," << s.source << "\n";
        }

        return oss.str();
    }

    static auto compute_statistics(const std::vector<Sample>& samples) -> std::string {
        std::map<std::string, size_t> label_counts;
        for (const auto& s : samples) {
            label_counts[s.label_name]++;
        }

        std::ostringstream oss;
        oss << "Dataset Statistics:\n";
        oss << "  Total samples: " << samples.size() << "\n";
        oss << "  Label distribution:\n";
        for (const auto& [name, count] : label_counts) {
            double pct = 100.0 * static_cast<double>(count) / samples.size();
            oss << "    " << name << ": " << count << " (" << std::fixed
                << std::setprecision(1) << pct << "%)\n";
        }

        if (!samples.empty()) {
            size_t n = samples[0].features.size();
            oss << "  Feature dimensions: " << n << "\n";

            std::vector<double> means(n, 0.0);
            for (const auto& s : samples) {
                for (size_t j = 0; j < n; ++j) {
                    means[j] += s.features[j];
                }
            }
            for (size_t j = 0; j < n; ++j) {
                means[j] /= samples.size();
            }

            oss << "  Feature means (first 10): ";
            for (size_t j = 0; j < std::min(n, size_t(10)); ++j) {
                if (j > 0) oss << ", ";
                oss << std::fixed << std::setprecision(4) << means[j];
            }
            oss << "\n";
        }

        return oss.str();
    }

private:
    std::mt19937 rng_;
    FeatureExtractorV3 extractor_;
    size_t total_generated_;

    auto gen_english_text() -> std::vector<uint8_t> {
        static const char* words[] = {
            "the", "quick", "brown", "fox", "jumps", "over", "lazy", "dog",
            "hello", "world", "data", "compression", "algorithm", "feature",
            "vector", "entropy", "random", "forest", "decision", "engine",
            "file", "type", "detection", "magic", "bytes", "extractor"
        };
        std::uniform_int_distribution<size_t> word_dist(0, 25);
        std::uniform_int_distribution<size_t> count_dist(50, 200);

        std::string text;
        size_t n = count_dist(rng_);
        for (size_t i = 0; i < n; ++i) {
            if (i > 0) text += " ";
            text += words[word_dist(rng_)];
            if (i % 10 == 9) text += ".\n";
        }

        return std::vector<uint8_t>(text.begin(), text.end());
    }

    auto gen_code_text() -> std::vector<uint8_t> {
        static const char* lines[] = {
            "int main() {\n",
            "    return 0;\n",
            "}\n",
            "for (int i = 0; i < n; i++) {\n",
            "    auto x = data[i];\n",
            "    result += x * x;\n",
            "}\n",
            "class FeatureExtractor {\n",
            "public:\n",
            "    auto extract() const;\n",
            "private:\n",
            "    std::vector<float> features_;\n",
            "};\n",
            "#include <vector>\n",
            "#include <algorithm>\n",
            "namespace ade {\n",
            "    void process();\n",
            "}\n"
        };
        std::uniform_int_distribution<size_t> line_dist(0, 17);
        std::uniform_int_distribution<size_t> count_dist(30, 100);

        std::string code;
        size_t n = count_dist(rng_);
        for (size_t i = 0; i < n; ++i) {
            code += lines[line_dist(rng_)];
        }

        return std::vector<uint8_t>(code.begin(), code.end());
    }

    auto gen_json_text() -> std::vector<uint8_t> {
        std::uniform_int_distribution<size_t> count_dist(20, 80);

        std::string json = "[\n";
        size_t n = count_dist(rng_);
        for (size_t i = 0; i < n; ++i) {
            json += "  {\"id\": " + std::to_string(i) +
                    ", \"name\": \"item_" + std::to_string(i) +
                    "\", \"value\": " + std::to_string(i * 3.14) + "}";
            if (i + 1 < n) json += ",";
            json += "\n";
        }
        json += "]\n";

        return std::vector<uint8_t>(json.begin(), json.end());
    }

    auto gen_repetitive_text() -> std::vector<uint8_t> {
        std::uniform_int_distribution<size_t> count_dist(100, 500);

        std::string pattern = "AAAAABBBBBCCCCCDDDDDEEEEE ";
        std::string text;
        size_t n = count_dist(rng_);
        for (size_t i = 0; i < n; ++i) {
            text += pattern;
        }

        return std::vector<uint8_t>(text.begin(), text.end());
    }

    auto gen_mixed_text() -> std::vector<uint8_t> {
        std::uniform_int_distribution<size_t> count_dist(50, 200);

        std::string text;
        size_t n = count_dist(rng_);
        for (size_t i = 0; i < n; ++i) {
            if (i % 3 == 0) text += "Hello World! ";
            else if (i % 3 == 1) text += "ABCDEFGH ";
            else text += "1234567890 ";
        }

        return std::vector<uint8_t>(text.begin(), text.end());
    }

    auto gen_random_bytes() -> std::vector<uint8_t> {
        std::uniform_int_distribution<size_t> size_dist(512, 8192);
        std::uniform_int_distribution<int> byte_dist(0, 255);

        size_t n = size_dist(rng_);
        std::vector<uint8_t> data(n);
        for (auto& b : data) b = static_cast<uint8_t>(byte_dist(rng_));
        return data;
    }

    auto gen_encrypted_like() -> std::vector<uint8_t> {
        std::uniform_int_distribution<size_t> size_dist(512, 4096);

        size_t n = size_dist(rng_);
        n = (n / 16) * 16;

        std::vector<uint8_t> data(n);
        std::uniform_int_distribution<uint64_t> dist;
        for (size_t i = 0; i < n; i += 8) {
            uint64_t val = dist(rng_);
            std::memcpy(&data[i], &val, std::min(size_t(8), n - i));
        }
        return data;
    }

    auto gen_already_compressed() -> std::vector<uint8_t> {
        std::uniform_int_distribution<size_t> size_dist(512, 4096);

        size_t n = size_dist(rng_);
        std::vector<uint8_t> data(n);

        std::uniform_int_distribution<int> byte_dist(0, 255);
        for (auto& b : data) b = static_cast<uint8_t>(byte_dist(rng_));

        data[0] = 0x78;
        data[1] = 0x9C;

        return data;
    }

    auto gen_uniform_distribution() -> std::vector<uint8_t> {
        std::uniform_int_distribution<size_t> size_dist(1024, 8192);

        size_t n = size_dist(rng_);
        std::vector<uint8_t> data(n);

        size_t count = 0;
        while (count < n) {
            uint8_t val = static_cast<uint8_t>(count % 256);
            data[count++] = val;
        }

        std::shuffle(data.begin(), data.end(), rng_);
        return data;
    }

    auto gen_run_length_data() -> std::vector<uint8_t> {
        std::uniform_int_distribution<size_t> size_dist(512, 4096);
        std::uniform_int_distribution<int> run_dist(10, 100);
        std::uniform_int_distribution<int> byte_dist(0, 15);

        std::vector<uint8_t> data;
        while (data.size() < size_dist(rng_)) {
            uint8_t val = static_cast<uint8_t>(byte_dist(rng_));
            size_t run = run_dist(rng_);
            for (size_t j = 0; j < run; ++j) data.push_back(val);
        }
        return data;
    }

    auto gen_sparse_data() -> std::vector<uint8_t> {
        std::uniform_int_distribution<size_t> size_dist(1024, 8192);

        size_t n = size_dist(rng_);
        std::vector<uint8_t> data(n, 0);

        std::uniform_int_distribution<size_t> pos_dist(0, n - 1);
        size_t non_zero = n / 20;
        for (size_t i = 0; i < non_zero; ++i) {
            data[pos_dist(rng_)] = static_cast<uint8_t>(rng_() % 256);
        }
        return data;
    }

    auto gen_bitmap_like() -> std::vector<uint8_t> {
        std::uniform_int_distribution<size_t> size_dist(1024, 4096);

        size_t n = size_dist(rng_);
        std::vector<uint8_t> data(n);

        uint8_t current = 0xFF;
        std::uniform_int_distribution<int> switch_dist(1, 32);
        size_t pos = 0;
        while (pos < n) {
            size_t run = switch_dist(rng_);
            for (size_t j = 0; j < run && pos < n; ++j) {
                data[pos++] = current;
            }
            current = ~current;
        }
        return data;
    }

    auto gen_dictionary_rich() -> std::vector<uint8_t> {
        static const char* phrases[] = {
            "function", "variable", "return", "class", "object",
            "method", "property", "value", "string", "integer"
        };
        std::uniform_int_distribution<size_t> phrase_dist(0, 9);
        std::uniform_int_distribution<size_t> count_dist(50, 200);

        std::string text;
        size_t n = count_dist(rng_);
        for (size_t i = 0; i < n; ++i) {
            text += phrases[phrase_dist(rng_)];
            text += " ";
        }

        return std::vector<uint8_t>(text.begin(), text.end());
    }

    auto gen_log_data() -> std::vector<uint8_t> {
        std::uniform_int_distribution<size_t> count_dist(30, 100);
        std::uniform_int_distribution<int> level_dist(0, 3);
        static const char* levels[] = {"INFO", "WARN", "ERROR", "DEBUG"};

        std::string text;
        size_t n = count_dist(rng_);
        for (size_t i = 0; i < n; ++i) {
            text += "[2025-01-15 10:30:";
            if (i % 60 < 10) text += "0";
            text += std::to_string(i % 60);
            text += "] ";
            text += levels[level_dist(rng_)];
            text += " Processing request #";
            text += std::to_string(i);
            text += " - status OK\n";
        }

        return std::vector<uint8_t>(text.begin(), text.end());
    }

    auto gen_csv_data() -> std::vector<uint8_t> {
        std::uniform_int_distribution<size_t> count_dist(50, 200);
        std::uniform_int_distribution<int> val_dist(0, 999);

        std::string text = "id,name,value,category,timestamp\n";
        size_t n = count_dist(rng_);
        for (size_t i = 0; i < n; ++i) {
            text += std::to_string(i) + ",";
            text += "item_" + std::to_string(i) + ",";
            text += std::to_string(val_dist(rng_)) + ",";
            text += "cat_" + std::to_string(i % 5) + ",";
            text += "2025-01-15T10:30:00\n";
        }

        return std::vector<uint8_t>(text.begin(), text.end());
    }

    auto gen_bmp_image() -> std::vector<uint8_t> {
        std::uniform_int_distribution<size_t> size_dist(4096, 16384);
        size_t data_size = size_dist(rng_);

        std::vector<uint8_t> data(data_size);

        data[0] = 'B'; data[1] = 'M';
        uint32_t file_size = static_cast<uint32_t>(data_size);
        std::memcpy(&data[2], &file_size, 4);
        uint32_t offset = 54;
        std::memcpy(&data[10], &offset, 4);
        uint32_t header_size = 40;
        std::memcpy(&data[14], &header_size, 4);
        int32_t width = 64, height = 64;
        std::memcpy(&data[18], &width, 4);
        std::memcpy(&data[22], &height, 4);
        uint16_t planes = 1, bpp = 24;
        std::memcpy(&data[26], &planes, 2);
        std::memcpy(&data[28], &bpp, 2);

        std::uniform_int_distribution<int> byte_dist(0, 255);
        for (size_t i = 54; i < data_size; ++i) {
            data[i] = static_cast<uint8_t>(byte_dist(rng_));
        }

        return data;
    }

    auto gen_wav_audio() -> std::vector<uint8_t> {
        std::uniform_int_distribution<size_t> size_dist(4096, 16384);
        size_t data_size = size_dist(rng_);

        std::vector<uint8_t> data(data_size);

        data[0] = 'R'; data[1] = 'I'; data[2] = 'F'; data[3] = 'F';
        uint32_t chunk_size = static_cast<uint32_t>(data_size - 8);
        std::memcpy(&data[4], &chunk_size, 4);
        data[8] = 'W'; data[9] = 'A'; data[10] = 'V'; data[11] = 'E';
        data[12] = 'f'; data[13] = 'm'; data[14] = 't'; data[15] = ' ';
        uint32_t fmt_size = 16;
        std::memcpy(&data[16], &fmt_size, 4);
        uint16_t audio_fmt = 1;
        std::memcpy(&data[20], &audio_fmt, 2);
        uint16_t channels = 1;
        std::memcpy(&data[22], &channels, 2);
        uint32_t sample_rate = 44100;
        std::memcpy(&data[24], &sample_rate, 4);
        uint32_t byte_rate = 88200;
        std::memcpy(&data[28], &byte_rate, 4);
        uint16_t block_align = 2;
        std::memcpy(&data[32], &block_align, 2);
        uint16_t bits_per_sample = 16;
        std::memcpy(&data[34], &bits_per_sample, 2);
        data[36] = 'd'; data[37] = 'a'; data[38] = 't'; data[39] = 'a';
        uint32_t audio_size = static_cast<uint32_t>(data_size - 44);
        std::memcpy(&data[40], &audio_size, 4);

        std::uniform_int_distribution<int16_t> sample_dist(-32768, 32767);
        for (size_t i = 44; i + 1 < data_size; i += 2) {
            int16_t sample = sample_dist(rng_);
            std::memcpy(&data[i], &sample, 2);
        }

        return data;
    }

    auto gen_png_like() -> std::vector<uint8_t> {
        std::uniform_int_distribution<size_t> size_dist(1024, 8192);
        size_t data_size = size_dist(rng_);

        std::vector<uint8_t> data(data_size);

        data[0] = 0x89; data[1] = 'P'; data[2] = 'N'; data[3] = 'G';
        data[4] = 0x0D; data[5] = 0x0A; data[6] = 0x1A; data[7] = 0x0A;

        std::uniform_int_distribution<int> byte_dist(0, 255);
        for (size_t i = 8; i < data_size; ++i) {
            data[i] = static_cast<uint8_t>(byte_dist(rng_));
        }

        return data;
    }

    auto gen_jpeg_like() -> std::vector<uint8_t> {
        std::uniform_int_distribution<size_t> size_dist(1024, 8192);
        size_t data_size = size_dist(rng_);

        std::vector<uint8_t> data(data_size);

        data[0] = 0xFF; data[1] = 0xD8;
        data[2] = 0xFF; data[3] = 0xE0;

        std::uniform_int_distribution<int> byte_dist(0, 255);
        for (size_t i = 4; i < data_size - 2; ++i) {
            data[i] = static_cast<uint8_t>(byte_dist(rng_));
        }
        data[data_size - 2] = 0xFF;
        data[data_size - 1] = 0xD9;

        return data;
    }

    auto gen_zip_like() -> std::vector<uint8_t> {
        std::uniform_int_distribution<size_t> size_dist(1024, 8192);
        size_t data_size = size_dist(rng_);

        std::vector<uint8_t> data(data_size);

        data[0] = 'P'; data[1] = 'K'; data[2] = 0x03; data[3] = 0x04;

        std::uniform_int_distribution<int> byte_dist(0, 255);
        for (size_t i = 4; i < data_size; ++i) {
            data[i] = static_cast<uint8_t>(byte_dist(rng_));
        }

        return data;
    }

    auto gen_gzip_like() -> std::vector<uint8_t> {
        std::uniform_int_distribution<size_t> size_dist(1024, 8192);
        size_t data_size = size_dist(rng_);

        std::vector<uint8_t> data(data_size);

        data[0] = 0x1F; data[1] = 0x8B; data[2] = 0x08;

        std::uniform_int_distribution<int> byte_dist(0, 255);
        for (size_t i = 3; i < data_size; ++i) {
            data[i] = static_cast<uint8_t>(byte_dist(rng_));
        }

        return data;
    }

    auto gen_zlib_like() -> std::vector<uint8_t> {
        std::uniform_int_distribution<size_t> size_dist(1024, 8192);
        size_t data_size = size_dist(rng_);

        std::vector<uint8_t> data(data_size);

        data[0] = 0x78; data[1] = 0x9C;

        std::uniform_int_distribution<int> byte_dist(0, 255);
        for (size_t i = 2; i < data_size; ++i) {
            data[i] = static_cast<uint8_t>(byte_dist(rng_));
        }

        return data;
    }

    auto gen_structured_text() -> std::vector<uint8_t> {
        static const char* templates[] = {
            "<!DOCTYPE html>\n<html>\n<head>\n<title>",
            "<?xml version=\"1.0\"?>\n<root>\n  <item id=\"",
            "{\n  \"name\": \"",
        };
        static const char* words[] = {
            "document", "section", "header", "content", "footer",
            "article", "sidebar", "navigation", "banner", "container",
        };

        std::ostringstream oss;
        std::uniform_int_distribution<int> tmpl_dist(0, 2);
        std::uniform_int_distribution<int> word_dist(0, 9);
        std::uniform_int_distribution<int> repeat_dist(3, 12);

        int tmpl_idx = tmpl_dist(rng_);
        oss << templates[tmpl_idx];

        int repeats = repeat_dist(rng_);
        for (int i = 0; i < repeats; ++i) {
            oss << words[word_dist(rng_)] << "_" << i;
            if (i + 1 < repeats) oss << "\",\n    \"";
        }
        oss << "\"\n}\n";

        std::string s = oss.str();
        return std::vector<uint8_t>(s.begin(), s.end());
    }

    auto gen_html_like() -> std::vector<uint8_t> {
        static const char* tags[] = {"div", "span", "p", "a", "li", "td", "th", "h1", "h2", "h3"};
        static const char* classes[] = {"container", "row", "col", "header", "footer",
                                        "nav", "main", "sidebar", "content", "wrapper"};

        std::ostringstream oss;
        std::uniform_int_distribution<int> tag_dist(0, 9);
        std::uniform_int_distribution<int> cls_dist(0, 9);
        std::uniform_int_distribution<int> repeat_dist(20, 60);

        oss << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n"
            << "<meta charset=\"UTF-8\">\n<title>Page</title>\n</head>\n<body>\n";

        int repeats = repeat_dist(rng_);
        for (int i = 0; i < repeats; ++i) {
            const char* tag = tags[tag_dist(rng_)];
            const char* cls = classes[cls_dist(rng_)];
            oss << "  <" << tag << " class=\"" << cls << "\">"
                << "Content block " << i
                << "</" << tag << ">\n";
        }
        oss << "</body>\n</html>\n";

        std::string s = oss.str();
        return std::vector<uint8_t>(s.begin(), s.end());
    }

    auto gen_xml_like() -> std::vector<uint8_t> {
        static const char* elem_names[] = {"record", "entry", "item", "node", "element",
                                           "field", "property", "member", "value", "data"};

        std::ostringstream oss;
        std::uniform_int_distribution<int> elem_dist(0, 9);
        std::uniform_int_distribution<int> repeat_dist(15, 50);

        oss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<dataset>\n";

        int repeats = repeat_dist(rng_);
        for (int i = 0; i < repeats; ++i) {
            const char* elem = elem_names[elem_dist(rng_)];
            oss << "  <" << elem << " id=\"" << i << "\">\n"
                << "    <name>entry_" << i << "</name>\n"
                << "    <value>" << (i * 137 % 1000) << "</value>\n"
                << "  </" << elem << ">\n";
        }
        oss << "</dataset>\n";

        std::string s = oss.str();
        return std::vector<uint8_t>(s.begin(), s.end());
    }

    auto gen_mixed_binary() -> std::vector<uint8_t> {
        std::uniform_int_distribution<size_t> size_dist(2048, 16384);
        size_t data_size = size_dist(rng_);
        std::vector<uint8_t> data(data_size);

        std::uniform_int_distribution<int> byte_dist(0, 255);
        std::uniform_int_distribution<int> pattern_dist(0, 3);

        size_t pos = 0;
        while (pos < data_size) {
            int pattern = pattern_dist(rng_);
            size_t block_len = std::min(size_t(64 + pattern * 32), data_size - pos);

            switch (pattern) {
                case 0:
                    for (size_t i = 0; i < block_len && pos < data_size; ++i, ++pos)
                        data[pos] = static_cast<uint8_t>(pos & 0xFF);
                    break;
                case 1:
                    for (size_t i = 0; i < block_len && pos < data_size; ++i, ++pos)
                        data[pos] = static_cast<uint8_t>(byte_dist(rng_));
                    break;
                case 2:
                    for (size_t i = 0; i < block_len && pos < data_size; ++i, ++pos)
                        data[pos] = static_cast<uint8_t>((pos * 7 + 13) & 0xFF);
                    break;
                case 3:
                    for (size_t i = 0; i < block_len && pos < data_size; ++i, ++pos)
                        data[pos] = static_cast<uint8_t>(0x00);
                    break;
            }
        }

        return data;
    }

    auto gen_semi_structured() -> std::vector<uint8_t> {
        std::ostringstream oss;
        std::uniform_int_distribution<int> repeat_dist(30, 80);

        oss << "# Configuration File\n";
        oss << "# Generated automatically\n\n";

        int repeats = repeat_dist(rng_);
        for (int i = 0; i < repeats; ++i) {
            oss << "[section_" << (i % 5) << "]\n";
            oss << "key_" << i << " = value_" << (i * 31 % 500) << "\n";
            oss << "flag_" << i << " = " << (i % 2 ? "true" : "false") << "\n";
            oss << "timeout_" << i << " = " << (i * 100 + 500) << "\n\n";
        }

        std::string s = oss.str();
        return std::vector<uint8_t>(s.begin(), s.end());
    }

    auto gen_log_like() -> std::vector<uint8_t> {
        static const char* levels[] = {"DEBUG", "INFO", "WARN", "ERROR"};
        static const char* modules[] = {"kernel", "network", "storage", "auth",
                                        "scheduler", "cache", "database", "api"};

        std::ostringstream oss;
        std::uniform_int_distribution<int> lvl_dist(0, 3);
        std::uniform_int_distribution<int> mod_dist(0, 7);
        std::uniform_int_distribution<int> repeat_dist(40, 100);

        int repeats = repeat_dist(rng_);
        for (int i = 0; i < repeats; ++i) {
            oss << "[2024-" << std::setw(2) << std::setfill('0') << (i % 12 + 1)
                << "-" << std::setw(2) << std::setfill('0') << (i % 28 + 1)
                << " " << std::setw(2) << (i % 24) << ":"
                << std::setw(2) << (i % 60) << ":"
                << std::setw(2) << (i % 60) << "] "
                << levels[lvl_dist(rng_)] << " ["
                << modules[mod_dist(rng_)] << "] "
                << "Processing request #" << (i * 100 + 1)
                << " from node_" << (i % 8)
                << " took " << (i % 500 + 1) << "ms\n";
        }

        std::string s = oss.str();
        return std::vector<uint8_t>(s.begin(), s.end());
    }
};

auto main(int argc, char* argv[]) -> int {
    size_t per_category = 100;
    std::string output_format = "json";
    std::string output_file;
    std::string model_file;
    bool train_model = false;
    bool evaluate = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-n" && i + 1 < argc) {
            per_category = std::stoul(argv[++i]);
        } else if (arg == "-f" && i + 1 < argc) {
            output_format = argv[++i];
        } else if (arg == "-o" && i + 1 < argc) {
            output_file = argv[++i];
        } else if (arg == "--train") {
            train_model = true;
        } else if (arg == "--evaluate") {
            evaluate = true;
        } else if (arg == "--save-model" && i + 1 < argc) {
            model_file = argv[++i];
            train_model = true;
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "ADE Dataset Generator v3.0\n\n"
                      << "Usage: generate_dataset [options]\n\n"
                      << "Options:\n"
                      << "  -n <count>       Samples per category (default: 100)\n"
                      << "  -f <format>      Output format: json, csv (default: json)\n"
                      << "  -o <file>        Output file path (default: stdout)\n"
                      << "  --train          Train RF model on generated data\n"
                      << "  --evaluate       Evaluate trained model on held-out data\n"
                      << "  --save-model <f> Save trained model to binary file\n"
                      << "  -h, --help       Show this help\n";
            return 0;
        }
    }

    std::cout << "=== ADE Dataset Generator ===\n";
    std::cout << "Generating " << per_category << " samples per category...\n\n";

    DatasetGenerator gen(42);
    auto samples = gen.generate_all(per_category);

    std::cout << DatasetGenerator::compute_statistics(samples) << "\n";

    if (train_model) {
        std::cout << "Training Random Forest model...\n";

        size_t train_size = samples.size() * 4 / 5;
        std::vector<TrainingSample> train_data;
        train_data.reserve(train_size);

        for (size_t i = 0; i < train_size; ++i) {
            TrainingSample ts;
            ts.features = samples[i].features;
            ts.label = samples[i].label;
            train_data.push_back(std::move(ts));
        }

        RandomForestConfig config;
        config.num_trees = 50;
        config.max_depth = 10;
        config.min_samples_split = 5;
        config.min_samples_leaf = 2;
        config.random_seed = 42;

        RandomForest rf;
        rf.train(train_data, config);

        std::cout << "  Trained " << config.num_trees << " trees\n";

        if (evaluate) {
            size_t correct = 0;
            size_t total = samples.size() - train_size;
            std::map<std::string, std::map<std::string, size_t>> confusion;

            for (size_t i = train_size; i < samples.size(); ++i) {
                int pred = rf.predict(samples[i].features);
                if (pred == samples[i].label) correct++;
                confusion[samples[i].label_name][algorithm_id_to_string(static_cast<AlgorithmID>(pred))]++;
            }

            double accuracy = static_cast<double>(correct) / total * 100.0;
            std::cout << "  Test accuracy: " << std::fixed << std::setprecision(1)
                      << accuracy << "% (" << correct << "/" << total << ")\n";

            std::cout << "\n  Confusion Matrix:\n";
            std::cout << "  Actual\\Predicted  ";
            std::vector<std::string> labels = {"DPFLATE", "SKIP", "LZSS", "DEFLATE", "BROTLI", "ZSTD"};
            for (const auto& l : labels) std::cout << std::setw(10) << l;
            std::cout << "\n";

            for (const auto& actual : labels) {
                std::cout << "  " << std::setw(16) << std::left << actual << std::right;
                for (const auto& predicted : labels) {
                    size_t count = confusion[actual][predicted];
                    std::cout << std::setw(10) << count;
                }
                std::cout << "\n";
            }
        }

        auto importance = rf.feature_importance();
        std::cout << "\n  Top 10 Feature Importances:\n";
        std::vector<std::pair<float, size_t>> imp_pairs;
        for (size_t i = 0; i < importance.size(); ++i) {
            imp_pairs.emplace_back(importance[i], i);
        }
        std::sort(imp_pairs.rbegin(), imp_pairs.rend());
        for (size_t i = 0; i < std::min(imp_pairs.size(), size_t(10)); ++i) {
            std::cout << "    Feature " << std::setw(2) << imp_pairs[i].second
                      << ": " << std::fixed << std::setprecision(4) << imp_pairs[i].first << "\n";
        }

        if (!model_file.empty()) {
            if (rf.save(model_file)) {
                std::cout << "\n  Model saved to: " << model_file << "\n";
            } else {
                std::cerr << "\n  Error: Failed to save model to: " << model_file << "\n";
                return 1;
            }
        }
    }

    if (!output_file.empty() || !train_model) {
        std::string output;
        if (output_format == "csv") {
            output = DatasetGenerator::samples_to_csv(samples);
        } else {
            output = DatasetGenerator::samples_to_json(samples);
        }

        if (!output_file.empty()) {
            std::ofstream ofs(output_file);
            if (ofs) {
                ofs << output;
                std::cout << "\nDataset written to: " << output_file << "\n";
            } else {
                std::cerr << "Error: Cannot open output file: " << output_file << "\n";
                return 1;
            }
        } else if (!train_model) {
            std::cout << output;
        }
    }

    std::cout << "\nDone. Total samples generated: " << gen.total_generated() << "\n";
    return 0;
}

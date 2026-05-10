#include "DecisionEngine.hpp"
#include "FeatureExtractorV3.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace compressor::ade;

struct BenchStats {
    size_t total_files{0};
    size_t success_count{0};
    size_t error_count{0};
    double total_time_ms{0};
    double min_time_ms{1e18};
    double max_time_ms{0};
    double total_file_size{0};

    std::vector<double> times;
    std::vector<size_t> sizes;

    auto add(double time_ms, size_t file_size, bool success) -> void {
        total_files++;
        if (success) {
            success_count++;
            total_time_ms += time_ms;
            total_file_size += static_cast<double>(file_size);
            times.push_back(time_ms);
            sizes.push_back(file_size);
            min_time_ms = std::min(min_time_ms, time_ms);
            max_time_ms = std::max(max_time_ms, time_ms);
        } else {
            error_count++;
        }
    }

    auto mean_time() const -> double {
        return times.empty() ? 0.0 : total_time_ms / times.size();
    }

    auto median_time() const -> double {
        if (times.empty()) return 0.0;
        auto sorted = times;
        std::sort(sorted.begin(), sorted.end());
        return sorted[sorted.size() / 2];
    }

    auto p95_time() const -> double {
        if (times.empty()) return 0.0;
        auto sorted = times;
        std::sort(sorted.begin(), sorted.end());
        size_t idx = static_cast<size_t>(sorted.size() * 0.95);
        return sorted[std::min(idx, sorted.size() - 1)];
    }

    auto throughput_mbps() const -> double {
        if (total_time_ms <= 0) return 0.0;
        return (total_file_size / (1024.0 * 1024.0)) / (total_time_ms / 1000.0);
    }

    auto summary() const -> std::string {
        std::ostringstream s;
        s << std::fixed << std::setprecision(3);
        s << "=== Benchmark Summary ===\n";
        s << "  Files processed: " << total_files
          << " (success: " << success_count << ", errors: " << error_count << ")\n";
        if (!times.empty()) {
            s << "  Total data: " << std::setprecision(2)
              << (total_file_size / (1024.0 * 1024.0)) << " MB\n";
            s << "  Total time: " << std::setprecision(3) << total_time_ms << " ms\n";
            s << "  Mean time: " << mean_time() << " ms\n";
            s << "  Median time: " << median_time() << " ms\n";
            s << "  P95 time: " << p95_time() << " ms\n";
            s << "  Min time: " << min_time_ms << " ms\n";
            s << "  Max time: " << max_time_ms << " ms\n";
            s << "  Throughput: " << std::setprecision(2) << throughput_mbps() << " MB/s\n";
        }
        return s.str();
    }
};

auto print_usage() -> void {
    std::cout << "ADE Feature Extractor Benchmark v3.0\n";
    std::cout << "====================================\n\n";
    std::cout << "Usage:\n";
    std::cout << "  bench_ade_extractor [options] <file_or_dir> [file_or_dir ...]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --json        Output feature vectors as JSON (default: summary only)\n";
    std::cout << "  --csv         Output results as CSV\n";
    std::cout << "  --verbose     Show per-file details\n";
    std::cout << "  --decide      Show compression decision\n";
    std::cout << "  --repeat N    Repeat extraction N times per file (default: 1)\n";
    std::cout << "  --help        Show this help message\n\n";
    std::cout << "Examples:\n";
    std::cout << "  bench_ade_extractor --verbose test.png\n";
    std::cout << "  bench_ade_extractor --json ./test_files/\n";
    std::cout << "  bench_ade_extractor --csv --repeat 5 ./corpus/\n";
}

auto collect_files(const std::vector<std::string>& paths) -> std::vector<std::string> {
    std::vector<std::string> files;
    for (const auto& p : paths) {
        if (fs::is_directory(p)) {
            for (const auto& entry : fs::recursive_directory_iterator(p)) {
                if (entry.is_regular_file()) {
                    files.push_back(entry.path().string());
                }
            }
        } else if (fs::is_regular_file(p)) {
            files.push_back(p);
        }
    }
    return files;
}

auto format_size(size_t bytes) -> std::string {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024) {
        std::ostringstream s;
        s << std::fixed << std::setprecision(1) << (bytes / 1024.0) << " KB";
        return s.str();
    }
    if (bytes < 1024ULL * 1024 * 1024) {
        std::ostringstream s;
        s << std::fixed << std::setprecision(1) << (bytes / (1024.0 * 1024.0)) << " MB";
        return s.str();
    }
    std::ostringstream s;
    s << std::fixed << std::setprecision(2) << (bytes / (1024.0 * 1024.0 * 1024.0)) << " GB";
    return s.str();
}

auto print_csv_header() -> void {
    std::cout << "filepath,file_size,file_type,ext_type,confidence,shannon_entropy,"
              << "min_entropy,unique_byte_ratio,printable_ratio,skewness,kurtosis,"
              << "unique_bigram_ratio,bigram_topk_conc,rle_potential,dict_potential,"
              << "extraction_time_ms\n";
}

auto print_csv_row(const std::string& filepath, const ExtractionResult& result) -> void {
    std::cout << "\"" << filepath << "\","
              << result.input_size << ","
              << file_type_to_string(result.detection.type) << ","
              << extension_type_to_string(result.detection.ext_type) << ","
              << std::fixed << std::setprecision(4)
              << result.detection.confidence << ","
              << result.vector.base.shannon_entropy << ","
              << result.vector.base.min_entropy << ","
              << result.vector.base.unique_byte_ratio << ","
              << result.vector.base.printable_ratio << ","
              << result.vector.base.skewness << ","
              << result.vector.base.kurtosis << ","
              << result.vector.base.unique_bigram_ratio << ","
              << result.vector.base.bigram_topk_conc << ","
              << result.vector.base.rle_potential << ","
              << result.vector.base.dict_potential << ","
              << std::setprecision(3) << result.extraction_time_ms << "\n";
}

auto print_verbose(const std::string& filepath, const ExtractionResult& result,
                   const RuleEngine& engine, bool show_decision) -> void {
    std::cout << "─── " << filepath << " ───\n";
    std::cout << "  Size: " << format_size(result.input_size) << "\n";
    std::cout << "  Type: " << file_type_to_string(result.detection.type)
              << " (confidence: " << std::fixed << std::setprecision(2)
              << result.detection.confidence * 100.0f << "%)\n";
    std::cout << "  Extension: " << extension_type_to_string(result.detection.ext_type)
              << " (" << static_cast<int>(result.vector.ext_dim) << " dims)\n";
    std::cout << "  Shannon Entropy: " << std::setprecision(4)
              << result.vector.base.shannon_entropy << " bits/byte\n";
    std::cout << "  Min Entropy: " << result.vector.base.min_entropy << "\n";
    std::cout << "  Unique Byte Ratio: " << result.vector.base.unique_byte_ratio << "\n";
    std::cout << "  Printable Ratio: " << result.vector.base.printable_ratio << "\n";
    std::cout << "  Skewness: " << result.vector.base.skewness << "\n";
    std::cout << "  Kurtosis: " << result.vector.base.kurtosis << "\n";
    std::cout << "  Bigram Uniqueness: " << result.vector.base.unique_bigram_ratio << "\n";
    std::cout << "  Bigram Top-K Conc: " << result.vector.base.bigram_topk_conc << "\n";
    std::cout << "  RLE Potential: " << result.vector.base.rle_potential << "\n";
    std::cout << "  Dict Potential: " << result.vector.base.dict_potential << "\n";
    std::cout << "  Extraction Time: " << std::setprecision(3)
              << result.extraction_time_ms << " ms\n";
    std::cout << "  Throughput: " << std::setprecision(2);
    if (result.extraction_time_ms > 0) {
        double mbps = (static_cast<double>(result.input_size) / (1024.0 * 1024.0))
                      / (result.extraction_time_ms / 1000.0);
        std::cout << mbps << " MB/s\n";
    } else {
        std::cout << "N/A\n";
    }
    if (show_decision) {
        auto decision = engine.decide(result.vector);
        std::cout << "  ── Decision ──\n";
        std::cout << "    Algorithm: " << algorithm_id_to_string(decision.algorithm) << "\n";
        std::cout << "    Est. Ratio: " << std::setprecision(2) << decision.estimated_ratio << "\n";
        std::cout << "    Confidence: " << decision.confidence << "\n";
        std::cout << "    Reason: " << decision.reason << "\n";
    }
    std::cout << "\n";
}

auto main(int argc, char* argv[]) -> int {
    bool json_output = false;
    bool csv_output = false;
    bool verbose = false;
    bool show_decision = false;
    int repeat = 1;
    std::vector<std::string> input_paths;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--json") {
            json_output = true;
        } else if (arg == "--csv") {
            csv_output = true;
        } else if (arg == "--verbose") {
            verbose = true;
        } else if (arg == "--decide") {
            show_decision = true;
        } else if (arg == "--repeat" && i + 1 < argc) {
            repeat = std::stoi(argv[++i]);
            if (repeat < 1) repeat = 1;
        } else if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else {
            input_paths.push_back(arg);
        }
    }

    if (input_paths.empty()) {
        print_usage();
        return 1;
    }

    auto files = collect_files(input_paths);
    if (files.empty()) {
        std::cerr << "No files found.\n";
        return 1;
    }

    std::cout << "ADE Feature Extractor Benchmark v3.0\n";
    std::cout << "Files to process: " << files.size() << "\n";
    std::cout << "Repeat count: " << repeat << "\n\n";

    if (csv_output) {
        print_csv_header();
    }

    FeatureExtractorV3 extractor;
    RuleEngine engine;
    BenchStats stats;
    std::vector<std::pair<std::string, CompressionDecision>> decisions;

    auto bench_start = std::chrono::high_resolution_clock::now();

    for (const auto& filepath : files) {
        std::ifstream check(filepath, std::ios::binary);
        if (!check.is_open()) {
            std::cerr << "Cannot open: " << filepath << "\n";
            stats.add(0, 0, false);
            continue;
        }
        check.close();

        ExtractionResult result;
        double best_time = 1e18;

        for (int r = 0; r < repeat; ++r) {
            auto r_result = extractor.extract_from_file(filepath);
            if (r == 0) {
                result = std::move(r_result);
            }
            best_time = std::min(best_time, result.extraction_time_ms);
        }

        result.extraction_time_ms = best_time;
        stats.add(result.extraction_time_ms, result.input_size, true);

        auto decision = engine.decide(result.vector);
        decisions.push_back({filepath, decision});

        if (json_output) {
            std::cout << result.to_json() << "\n";
            std::cout << decision.to_json() << "\n";
        } else if (csv_output) {
            print_csv_row(filepath, result);
        } else if (verbose) {
            print_verbose(filepath, result, engine, show_decision);
        }
    }

    auto bench_end = std::chrono::high_resolution_clock::now();
    double wall_time_ms =
        std::chrono::duration<double, std::milli>(bench_end - bench_start).count();

    if (!csv_output && !json_output) {
        std::cout << stats.summary();
        std::cout << "  Wall time: " << std::fixed << std::setprecision(3)
                  << wall_time_ms << " ms\n\n";

        if (show_decision || !verbose) {
            std::cout << "=== Decision Summary ===\n";
            for (const auto& [fp, dec] : decisions) {
                std::cout << "  " << algorithm_id_to_string(dec.algorithm)
                          << " (est.ratio=" << std::setprecision(2) << dec.estimated_ratio
                          << ", conf=" << dec.confidence << ") ← "
                          << fp.substr(fp.find_last_of("/\\") + 1) << "\n";
            }
            std::cout << "\n";
        }

        std::cout << FeatureExtractorV3::get_memory_statistics();
    }

    return 0;
}

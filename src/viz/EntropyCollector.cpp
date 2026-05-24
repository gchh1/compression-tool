#include "EntropyCollector.hpp"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace compressor::viz {

EntropyCollector::EntropyCollector(const std::string& heat_path,
                                   uint32_t chunk_bytes)
    : heat_path_(heat_path)
    , chunk_bytes_(chunk_bytes)
{
    tmp_path_ = heat_path_ + ".part";
    entropy_buf_.reserve(kBufferFlushCount);
    tmp_file_.open(tmp_path_, std::ios::binary | std::ios::trunc);
}

EntropyCollector::~EntropyCollector() {
    if (tmp_file_.is_open())
        tmp_file_.close();
    std::error_code ec;
    fs::remove(tmp_path_, ec);
}

float EntropyCollector::computeEntropy(const uint8_t* data, size_t size) {
    if (size == 0) return 0.0f;

    uint32_t counts[256] = {};
    for (size_t i = 0; i < size; ++i)
        counts[data[i]]++;

    float entropy = 0.0f;
    const float inv_size = 1.0f / static_cast<float>(size);
    for (int b = 0; b < 256; ++b) {
        if (counts[b] == 0) continue;
        float p = static_cast<float>(counts[b]) * inv_size;
        entropy -= p * std::log2(p);
    }
    return entropy;
}

float EntropyCollector::avg_entropy() const {
    if (total_chunks_ == 0) return 0.0f;
    return static_cast<float>(entropy_sum_ / static_cast<double>(total_chunks_));
}

void EntropyCollector::onRawChunk(const uint8_t* data, size_t size) {
    if (size == 0) return;

    float e = computeEntropy(data, size);
    entropy_buf_.push_back(e);
    entropy_sum_ += static_cast<double>(e);
    total_input_ += static_cast<uint64_t>(size);
    total_chunks_++;

    if (entropy_buf_.size() >= kBufferFlushCount)
        flushEntropyBuffer();
}

void EntropyCollector::onChunkOutput(size_t output_bytes) {
    chunk_output_sizes_.push_back(static_cast<uint32_t>(output_bytes));
    total_output_ += static_cast<uint64_t>(output_bytes);
}

void EntropyCollector::flushEntropyBuffer() {
    if (entropy_buf_.empty()) return;

    size_t nbytes = entropy_buf_.size() * sizeof(float);
    tmp_file_.write(reinterpret_cast<const char*>(entropy_buf_.data()),
                    static_cast<std::streamsize>(nbytes));
    entropy_buf_.clear();
}

void EntropyCollector::onCompressionFinish() {
    flushEntropyBuffer();

    if (tmp_file_.is_open())
        tmp_file_.close();

    // ── Determine version ──
    // v3: per-chunk compression ratios (when output data is available).
    // v2: per-chunk Shannon entropy (fallback for callers that don't
    //     supply output sizes).
    const bool use_v3 = !chunk_output_sizes_.empty();

    std::ofstream out(heat_path_, std::ios::binary | std::ios::trunc);
    if (!out) return;

    uint32_t magic = kMagic;
    uint32_t version = use_v3 ? kVersion3 : kVersion2;
    uint32_t total_chunks = total_chunks_;
    uint32_t chunk_bytes = chunk_bytes_;

    out.write(reinterpret_cast<const char*>(&magic), 4);
    out.write(reinterpret_cast<const char*>(&version), 4);
    out.write(reinterpret_cast<const char*>(&total_chunks), 4);
    out.write(reinterpret_cast<const char*>(&chunk_bytes), 4);

    if (use_v3) {
        // v3 header tail: total_input (u64) + total_output (u64)
        out.write(reinterpret_cast<const char*>(&total_input_), 8);
        out.write(reinterpret_cast<const char*>(&total_output_), 8);

        // ── Write per-chunk compression ratios ──
        // Read entropy values from temp file (we only need them for the
        // count; the actual per-chunk data is compression_ratio).
        std::ifstream tmp(tmp_path_, std::ios::binary);
        std::vector<float> entropies(total_chunks);
        if (tmp) {
            tmp.read(reinterpret_cast<char*>(entropies.data()),
                     static_cast<std::streamsize>(total_chunks * sizeof(float)));
        }

        for (uint32_t i = 0; i < total_chunks; ++i) {
            uint32_t out_sz = (i < chunk_output_sizes_.size()) ? chunk_output_sizes_[i] : 0;
            // chunk_input = min(chunk_bytes, total_input - i*chunk_bytes)
            uint64_t offset = static_cast<uint64_t>(i) * chunk_bytes;
            uint32_t in_sz = static_cast<uint32_t>(
                std::min<uint64_t>(chunk_bytes, total_input_ - offset));
            float ratio = (in_sz > 0) ? static_cast<float>(out_sz) / static_cast<float>(in_sz)
                                      : 1.0f;
            out.write(reinterpret_cast<const char*>(&ratio), 4);
        }
    } else {
        // v2 header tail: avg_entropy (f32)
        float avg_e = avg_entropy();
        out.write(reinterpret_cast<const char*>(&avg_e), 4);

        // Copy temp file (chunk entropy data) into final file
        std::ifstream tmp(tmp_path_, std::ios::binary);
        if (tmp) {
            char buf[65536];
            while (tmp.read(buf, sizeof(buf)).gcount() > 0)
                out.write(buf, tmp.gcount());
        }
    }

    out.close();

    std::error_code ec;
    fs::remove(tmp_path_, ec);
}

}  // namespace compressor::viz

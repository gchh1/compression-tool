#include "EntropyCollector.hpp"

#include "BackgroundWriter.hpp"

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
}

EntropyCollector::~EntropyCollector() {
    if (writer_) {
        writer_->stop();
        writer_.reset();
    }
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

void EntropyCollector::ensureWriter() {
    if (writer_) return;
    writer_ = std::make_unique<BackgroundWriter>(tmp_path_);
}

void EntropyCollector::onRawChunk(const uint8_t* data, size_t size) {
    if (size == 0) return;

    float e = computeEntropy(data, size);
    entropy_buf_.push_back(e);
    entropy_sum_ += static_cast<double>(e);
    total_chunks_++;

    if (entropy_buf_.size() >= kBufferFlushCount)
        flushEntropyBuffer();
}

void EntropyCollector::flushEntropyBuffer() {
    if (entropy_buf_.empty()) return;
    ensureWriter();

    size_t nbytes = entropy_buf_.size() * sizeof(float);
    auto buf = std::make_shared<std::vector<uint8_t>>(nbytes);
    std::memcpy(buf->data(), entropy_buf_.data(), nbytes);
    writer_->submit(std::move(buf), nbytes);
    entropy_buf_.clear();
}

void EntropyCollector::onCompressionFinish() {
    flushEntropyBuffer();

    if (writer_) {
        writer_->stop();
        writer_.reset();
    }

    // ── Assemble .heat v2 file ────────────────────────
    // Header (20 bytes): magic(4) + version(4) + total_chunks(4) +
    //                     chunk_bytes(4) + avg_entropy(4)
    // Chunk Data: [total_chunks × f32 entropy]

    std::ofstream out(heat_path_, std::ios::binary | std::ios::trunc);
    if (!out) return;

    uint32_t magic = kMagic;
    uint32_t version = kVersion;
    uint32_t total_chunks = total_chunks_;
    uint32_t chunk_bytes = chunk_bytes_;
    float avg_e = avg_entropy();

    out.write(reinterpret_cast<const char*>(&magic), 4);
    out.write(reinterpret_cast<const char*>(&version), 4);
    out.write(reinterpret_cast<const char*>(&total_chunks), 4);
    out.write(reinterpret_cast<const char*>(&chunk_bytes), 4);
    out.write(reinterpret_cast<const char*>(&avg_e), 4);

    // Copy temp file (chunk entropy data) into final file
    {
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

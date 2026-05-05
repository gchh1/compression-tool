#include "api.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "DataChunk.hpp"
#include "MemoryPool.hpp"
#include "PackReader.hpp"
#include "PackWriter.hpp"
#include "Pipeline.hpp"

#ifndef __EMSCRIPTEN__
#include <filesystem>
#include <fstream>
#include "FileReader.hpp"

namespace fs = std::filesystem;
#endif

namespace compressor::api {

// ---- internal helper: in-memory IDataReader ----

namespace {

class VectorReader : public archiver::IDataReader {
   public:
    explicit VectorReader(const std::vector<uint8_t>& d) : data_(d) {}

    auto read(uint64_t offset, std::span<uint8_t> buffer) -> size_t override {
        if (offset >= data_.size()) return 0;
        size_t n = std::min(static_cast<size_t>(data_.size() - offset),
                            buffer.size());
        std::memcpy(buffer.data(), data_.data() + offset, n);
        return n;
    }

    auto size() const -> uint64_t override { return data_.size(); }

   private:
    const std::vector<uint8_t>& data_;
};

#ifndef __EMSCRIPTEN__
constexpr size_t STREAMING_CHUNK_SIZE = 1048576;  // 1 MB
#endif

}  // namespace

// ---- public API ----

auto compress(const std::vector<uint8_t>& data,
              std::span<const AlgorithmID> chain) -> CompressResult {
    CompressResult result;
    result.original_size = data.size();

    auto t0 = std::chrono::high_resolution_clock::now();

    std::vector<std::unique_ptr<algorithm::IAlgorithm>> algos;
    for (auto id : chain) {
        if (auto a = core::createAlgorithm(id)) algos.push_back(std::move(a));
    }
    if (algos.empty()) {
        result.error_message = "Unknown or null algorithm";
        return result;
    }

    auto pool = std::make_shared<memory::MemoryPool>(4, 65536);
    processor::Pipeline pipeline(std::move(algos), pool);
    pipeline.push(data, true);
    pipeline.finish();

    while (true) {
        auto chunk = pipeline.pull();
        if (chunk.empty()) break;
        auto v = chunk.view();
        result.data.insert(result.data.end(), v.begin(), v.end());
    }

    result.block_profile = pipeline.getBlockProfile();

    auto t1 = std::chrono::high_resolution_clock::now();
    result.time_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    result.compressed_size = result.data.size();
    result.compression_ratio =
        result.original_size > 0
            ? static_cast<double>(result.compressed_size) / result.original_size
            : 0.0;
    result.success = true;
    return result;
}

auto decompress(const std::vector<uint8_t>& data,
                std::span<const AlgorithmID> chain) -> CompressResult {
    CompressResult result;
    result.original_size = data.size();

    auto t0 = std::chrono::high_resolution_clock::now();

    std::vector<std::unique_ptr<algorithm::IAlgorithm>> algos;
    for (auto id : chain) {
        if (auto a = core::createAlgorithm(id)) algos.push_back(std::move(a));
    }
    if (algos.empty()) {
        result.error_message = "Unknown or null algorithm";
        return result;
    }

    auto pool = std::make_shared<memory::MemoryPool>(4, 65536);
    processor::Pipeline pipeline(std::move(algos), pool);
    pipeline.push(data, true);
    pipeline.finish();

    while (true) {
        auto chunk = pipeline.pull();
        if (chunk.empty()) break;
        auto v = chunk.view();
        result.data.insert(result.data.end(), v.begin(), v.end());
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    result.time_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    result.compressed_size = result.data.size();
    result.compression_ratio =
        result.original_size > 0
            ? static_cast<double>(result.compressed_size) / result.original_size
            : 0.0;
    result.success = true;
    return result;
}

auto packAndCompress(const std::vector<WebFile>& files,
                     std::span<const AlgorithmID> chain)
    -> std::vector<uint8_t> {
    auto pool = std::make_shared<memory::MemoryPool>(8, 65536);
    archiver::PackWriter writer(pool);

    std::vector<uint8_t> result;

    for (const auto& f : files) {
        writer.beginFile(f.name, chain);
        writer.pushFileData(f.content);
        writer.endFile();

        // Drain output before starting next file (beginFile clears buffers)
        while (true) {
            auto out = writer.pullOutput();
            if (out.empty()) break;
            result.insert(result.end(), out.begin(), out.end());
            writer.consumeOutput(out.size());
        }
    }
    writer.finish();

    // Drain any remaining output after finish
    while (true) {
        auto out = writer.pullOutput();
        if (out.empty()) break;
        result.insert(result.end(), out.begin(), out.end());
        writer.consumeOutput(out.size());
    }
    return result;
}

auto decompressAndUnpack(const std::vector<uint8_t>& data)
    -> std::vector<WebFile> {
    auto reader = std::make_unique<VectorReader>(data);
    archiver::PackReader pack_reader(std::move(reader));

    std::vector<WebFile> result;
    const auto& entries = pack_reader.getEntries();

    for (size_t i = 0; i < entries.size(); ++i) {
        auto pipeline = pack_reader.extractStream(i);
        if (!pipeline) continue;

        WebFile wf;
        wf.name = entries[i].filepath;

        while (true) {
            auto chunk = pipeline->pull();
            if (chunk.empty()) break;
            auto v = chunk.view();
            wf.content.insert(wf.content.end(), v.begin(), v.end());
        }
        result.push_back(std::move(wf));
    }
    return result;
}

#ifndef __EMSCRIPTEN__

// ---- streaming file API ----

auto compressFile(const std::string& input_path,
                  const std::string& output_path,
                  std::span<const AlgorithmID> chain) -> CompressResult {
    CompressResult result;

    auto t0 = std::chrono::high_resolution_clock::now();

    std::error_code ec;
    result.original_size = fs::file_size(input_path, ec);
    if (ec) {
        result.error_message = "Cannot open input: " + ec.message();
        return result;
    }

    std::vector<std::unique_ptr<algorithm::IAlgorithm>> algos;
    for (auto id : chain)
        if (auto a = core::createAlgorithm(id)) algos.push_back(std::move(a));
    if (algos.empty()) {
        result.error_message = "Unknown or null algorithm";
        return result;
    }

    auto pool =
        std::make_shared<memory::MemoryPool>(4, STREAMING_CHUNK_SIZE);
    processor::Pipeline pipeline(std::move(algos), pool);

    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
        result.error_message = "Cannot open input file";
        return result;
    }

    std::ofstream output(output_path,
                         std::ios::binary | std::ios::trunc);
    if (!output) {
        result.error_message = "Cannot open output file";
        return result;
    }

    std::vector<uint8_t> buf(STREAMING_CHUNK_SIZE);
    uint64_t bytes_read = 0;
    uint64_t total_written = 0;

    while (bytes_read < result.original_size) {
        size_t to_read = std::min(STREAMING_CHUNK_SIZE,
                                  static_cast<size_t>(result.original_size -
                                                      bytes_read));
        input.read(reinterpret_cast<char*>(buf.data()),
                   static_cast<std::streamsize>(to_read));
        size_t actual = static_cast<size_t>(input.gcount());
        if (actual == 0) break;

        bool is_last = (bytes_read + actual >= result.original_size);
        pipeline.push(std::span<const uint8_t>(buf.data(), actual), is_last);
        bytes_read += actual;

        // Drain output
        while (true) {
            auto chunk = pipeline.pull();
            if (chunk.empty()) break;
            auto v = chunk.view();
            output.write(reinterpret_cast<const char*>(v.data()),
                         static_cast<std::streamsize>(v.size()));
            total_written += v.size();
        }
    }

    pipeline.finish();

    // Final drain
    while (true) {
        auto chunk = pipeline.pull();
        if (chunk.empty()) break;
        auto v = chunk.view();
        output.write(reinterpret_cast<const char*>(v.data()),
                     static_cast<std::streamsize>(v.size()));
        total_written += v.size();
    }

    result.block_profile = pipeline.getBlockProfile();

    auto t1 = std::chrono::high_resolution_clock::now();
    result.time_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    result.compressed_size = total_written;
    result.compression_ratio =
        result.original_size > 0
            ? static_cast<double>(total_written) / result.original_size
            : 0.0;
    result.success = true;
    return result;
}

auto decompressFile(const std::string& input_path,
                    const std::string& output_path,
                    std::span<const AlgorithmID> chain) -> CompressResult {
    CompressResult result;

    auto t0 = std::chrono::high_resolution_clock::now();

    std::error_code ec;
    result.original_size = fs::file_size(input_path, ec);
    if (ec) {
        result.error_message = "Cannot open input: " + ec.message();
        return result;
    }

    std::vector<std::unique_ptr<algorithm::IAlgorithm>> algos;
    for (auto id : chain)
        if (auto a = core::createAlgorithm(id)) algos.push_back(std::move(a));
    if (algos.empty()) {
        result.error_message = "Unknown or null algorithm";
        return result;
    }

    auto pool =
        std::make_shared<memory::MemoryPool>(4, STREAMING_CHUNK_SIZE);
    processor::Pipeline pipeline(std::move(algos), pool);

    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
        result.error_message = "Cannot open input file";
        return result;
    }

    std::ofstream output(output_path,
                         std::ios::binary | std::ios::trunc);
    if (!output) {
        result.error_message = "Cannot open output file";
        return result;
    }

    std::vector<uint8_t> buf(STREAMING_CHUNK_SIZE);
    uint64_t bytes_read = 0;
    uint64_t total_written = 0;

    while (bytes_read < result.original_size) {
        size_t to_read = std::min(STREAMING_CHUNK_SIZE,
                                  static_cast<size_t>(result.original_size -
                                                      bytes_read));
        input.read(reinterpret_cast<char*>(buf.data()),
                   static_cast<std::streamsize>(to_read));
        size_t actual = static_cast<size_t>(input.gcount());
        if (actual == 0) break;

        bool is_last = (bytes_read + actual >= result.original_size);
        pipeline.push(std::span<const uint8_t>(buf.data(), actual), is_last);
        bytes_read += actual;

        while (true) {
            auto chunk = pipeline.pull();
            if (chunk.empty()) break;
            auto v = chunk.view();
            output.write(reinterpret_cast<const char*>(v.data()),
                         static_cast<std::streamsize>(v.size()));
            total_written += v.size();
        }
    }

    pipeline.finish();

    while (true) {
        auto chunk = pipeline.pull();
        if (chunk.empty()) break;
        auto v = chunk.view();
        output.write(reinterpret_cast<const char*>(v.data()),
                     static_cast<std::streamsize>(v.size()));
        total_written += v.size();
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    result.time_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    result.compressed_size = total_written;
    result.compression_ratio =
        result.original_size > 0
            ? static_cast<double>(total_written) / result.original_size
            : 0.0;
    result.success = true;
    return result;
}

auto compressDirectory(const std::string& dir_path,
                       const std::string& output_path,
                       std::span<const AlgorithmID> chain) -> CompressResult {
    CompressResult result;

    auto t0 = std::chrono::high_resolution_clock::now();

    std::error_code ec;
    if (!fs::exists(dir_path, ec) || !fs::is_directory(dir_path, ec)) {
        result.error_message = "Not a directory: " + dir_path;
        return result;
    }

    // Collect files
    std::vector<std::pair<fs::path, fs::path>> files;  // abs, rel
    for (auto it = fs::recursive_directory_iterator(dir_path, ec);
         it != fs::recursive_directory_iterator(); ++it) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file()) continue;
        fs::path abs = it->path();
        fs::path rel = fs::relative(abs, dir_path, ec);
        if (ec) {
            rel = abs.filename();
            ec.clear();
        }
        files.emplace_back(std::move(abs), std::move(rel));
    }

    if (files.empty()) {
        result.error_message = "No files found in directory";
        return result;
    }

    std::ofstream output(output_path,
                         std::ios::binary | std::ios::trunc);
    if (!output) {
        result.error_message = "Cannot open output file";
        return result;
    }

    auto pool = std::make_shared<memory::MemoryPool>(8, STREAMING_CHUNK_SIZE);
    archiver::PackWriter writer(pool);

    uint64_t total_original = 0;
    uint64_t total_written = 0;
    std::vector<uint8_t> buf(STREAMING_CHUNK_SIZE);

    for (const auto& [abs_path, rel_path] : files) {
        std::error_code fec;
        auto file_size = fs::file_size(abs_path, fec);
        if (fec) continue;

        writer.beginFile(rel_path.string(), chain);

        std::ifstream input(abs_path, std::ios::binary);
        if (!input) continue;

        uint64_t bytes_read = 0;
        while (bytes_read < file_size) {
            size_t to_read =
                std::min(STREAMING_CHUNK_SIZE,
                         static_cast<size_t>(file_size - bytes_read));
            input.read(reinterpret_cast<char*>(buf.data()),
                       static_cast<std::streamsize>(to_read));
            size_t actual = static_cast<size_t>(input.gcount());
            if (actual == 0) break;
            writer.pushFileData(std::span<const uint8_t>(buf.data(), actual));
            bytes_read += actual;
        }

        writer.endFile();
        total_original += file_size;

        // Drain writer output
        while (true) {
            auto out = writer.pullOutput();
            if (out.empty()) break;
            output.write(reinterpret_cast<const char*>(out.data()),
                         static_cast<std::streamsize>(out.size()));
            total_written += out.size();
            writer.consumeOutput(out.size());
        }
    }

    writer.finish();

    // Final drain
    while (true) {
        auto out = writer.pullOutput();
        if (out.empty()) break;
        output.write(reinterpret_cast<const char*>(out.data()),
                     static_cast<std::streamsize>(out.size()));
        total_written += out.size();
        writer.consumeOutput(out.size());
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    result.time_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    result.original_size = total_original;
    result.compressed_size = total_written;
    result.compression_ratio =
        total_original > 0
            ? static_cast<double>(total_written) / total_original
            : 0.0;
    result.success = true;
    return result;
}

auto decompressAndUnpackToDisk(const std::string& input_path,
                                const std::string& output_dir) -> CompressResult {
    CompressResult result;

    auto t0 = std::chrono::high_resolution_clock::now();

    auto reader = std::make_unique<archiver::FileReader>(input_path);
    if (reader->size() == 0) {
        result.error_message = "Cannot open archive: " + input_path;
        return result;
    }

    archiver::PackReader pack_reader(std::move(reader));
    const auto& entries = pack_reader.getEntries();

    if (entries.empty()) {
        result.error_message = "Empty archive";
        return result;
    }

    fs::path out_dir(output_dir);
    std::error_code ec;
    fs::create_directories(out_dir, ec);

    uint64_t total_original = 0;
    uint64_t total_written = 0;

    for (size_t i = 0; i < entries.size(); ++i) {
        auto pipeline = pack_reader.extractStream(i);
        if (!pipeline) continue;

        fs::path out_path = out_dir / entries[i].filepath;
        fs::create_directories(out_path.parent_path(), ec);
        if (ec) {
            result.error_message = "Failed to create directory: " +
                                   out_path.parent_path().string();
            continue;
        }

        std::ofstream output(out_path,
                             std::ios::binary | std::ios::trunc);
        if (!output) continue;

        total_original += entries[i].original_size;
        while (true) {
            auto chunk = pipeline->pull();
            if (chunk.empty()) break;
            auto v = chunk.view();
            output.write(reinterpret_cast<const char*>(v.data()),
                         static_cast<std::streamsize>(v.size()));
            total_written += v.size();
        }
    }

    auto t1 = std::chrono::high_resolution_clock::now();
    result.time_ms =
        std::chrono::duration<double, std::milli>(t1 - t0).count();
    result.original_size = total_original;
    result.compressed_size = total_written;
    result.compression_ratio =
        total_original > 0
            ? static_cast<double>(total_written) / total_original
            : 0.0;
    result.success = true;
    return result;
}

#endif  // __EMSCRIPTEN__

}  // namespace compressor::api

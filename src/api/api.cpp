#include "api.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <vector>

#include "DataChunk.hpp"
#include "MemoryPool.hpp"
#include "PackReader.hpp"
#include "PackWriter.hpp"
#include "Pipeline.hpp"
#include "WCXProtocol.hpp"

#ifndef __EMSCRIPTEN__
#include <filesystem>
#include <fstream>
#include "FileReader.hpp"
#include "wcx_decompress_input.hpp"

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

auto pack_wcx(const std::vector<uint8_t>& compressed_data,
              AlgorithmID algorithm,
              size_t original_size,
              const std::string& original_filename,
              bool is_folder) -> std::vector<uint8_t> {
    uint8_t code = wcx::toAlgoCode(algorithm);
    auto orig_u32 = static_cast<uint32_t>(std::min<size_t>(original_size, UINT32_MAX));
    auto comp_u32 = static_cast<uint32_t>(std::min<size_t>(compressed_data.size(), UINT32_MAX));
    auto out = wcx::buildHeaderBytes(code, orig_u32, comp_u32, original_filename);
    if (is_folder && out.size() >= 15) {
        out[14] = static_cast<uint8_t>(1);  // FLAG_FOLDER
    }
    out.insert(out.end(), compressed_data.begin(), compressed_data.end());
    return out;
}

auto unpack_wcx(const std::vector<uint8_t>& data) -> WCXUnpackResult {
    WCXUnpackResult result;
    wcx::HeaderView header{};
    if (!wcx::tryParseHeader(std::span<const uint8_t>(data.data(), data.size()),
                             header) ||
        !header.valid) {
        result.error_message = "Invalid WCX header";
        return result;
    }
    result.algo_code = header.algo_code;
    result.original_size = header.original_size;
    result.compressed_size = header.compressed_size;
    result.original_filename = header.original_filename;
    result.is_folder = (header.flags & 0x01) != 0;
    if (header.total_size > data.size()) {
        result.error_message = "WCX payload offset out of range";
        return result;
    }
    const uint64_t need = static_cast<uint64_t>(header.total_size) +
                          static_cast<uint64_t>(header.compressed_size);
    if (need > data.size()) {
        result.error_message =
            "WCX data shorter than declared header + compressed_size";
        return result;
    }
    const size_t payload_begin = header.total_size;
    const size_t payload_end = static_cast<size_t>(need);
    result.payload.assign(
        data.begin() + static_cast<std::ptrdiff_t>(payload_begin),
        data.begin() + static_cast<std::ptrdiff_t>(payload_end));
    result.success = true;
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

    std::string original_filename = fs::path(input_path).filename().string();
    uint8_t algo_code = chain.empty() ? 0 : wcx::toAlgoCode(chain.front());
    auto header_orig_u32 =
        static_cast<uint32_t>(std::min<uint64_t>(result.original_size, UINT32_MAX));
    if (!wcx::writeHeader(output, algo_code, header_orig_u32, 0, original_filename)) {
        result.error_message = "Cannot write WCX header";
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

    // Patch compressed_size in WCX header at byte offset 10.
    auto comp_u32 = static_cast<uint32_t>(std::min<uint64_t>(total_written, UINT32_MAX));
    wcx::patchCompressedSize(output, comp_u32);
    output.flush();

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

    const uint64_t file_on_disk = result.original_size;
    auto payload_size_opt =
        resolve_wcx_file_stream_payload_length(input_path, input, file_on_disk, result);
    if (!payload_size_opt) {
        return result;
    }
    uint64_t payload_size = *payload_size_opt;

    const std::streampos pos_after_hdr = input.tellg();
    if (pos_after_hdr < std::streampos{0}) {
        result.error_message = "Cannot determine WCX payload offset";
        return result;
    }
    const uint64_t payload_begin =
        static_cast<uint64_t>(pos_after_hdr);
    if (payload_begin > file_on_disk) {
        result.error_message = "Invalid WCX header span";
        return result;
    }
    const uint64_t remaining = file_on_disk - payload_begin;
    if (remaining < payload_size) {
        result.error_message =
            "WCX file shorter than declared compressed_size";
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

    while (bytes_read < payload_size) {
        size_t to_read = std::min(STREAMING_CHUNK_SIZE,
                                  static_cast<size_t>(payload_size -
                                                      bytes_read));
        input.read(reinterpret_cast<char*>(buf.data()),
                   static_cast<std::streamsize>(to_read));
        size_t actual = static_cast<size_t>(input.gcount());
        if (actual == 0) break;

        bool is_last = (bytes_read + actual >= payload_size);
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

    if (bytes_read != payload_size) {
        result.error_message = "WCX payload truncated";
        auto t1 = std::chrono::high_resolution_clock::now();
        result.time_ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        return result;
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

    // Collect files with known original sizes so WCX header can record total size.
    struct SourceFile {
        fs::path abs_path;
        fs::path rel_path;
        uint64_t file_size{0};
    };
    std::vector<SourceFile> files;
    uint64_t total_original = 0;
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
        std::error_code fec;
        auto file_size = fs::file_size(abs, fec);
        if (fec) continue;

        files.push_back(SourceFile{
            std::move(abs),
            std::move(rel),
            static_cast<uint64_t>(file_size),
        });
        total_original += static_cast<uint64_t>(file_size);
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

    const std::string original_filename = fs::path(dir_path).filename().string();
    uint8_t algo_code = chain.empty() ? 0 : wcx::toAlgoCode(chain.front());
    auto header_orig_u32 =
        static_cast<uint32_t>(std::min<uint64_t>(total_original, UINT32_MAX));
    if (!wcx::writeHeader(output, algo_code, header_orig_u32, 0, original_filename)) {
        result.error_message = "Cannot write WCX header";
        return result;
    }
    output.seekp(14, std::ios::beg);
    output.put(static_cast<char>(0x01));  // FLAG_FOLDER
    output.seekp(0, std::ios::end);

    uint64_t total_written = 0;
    std::vector<uint8_t> buf(STREAMING_CHUNK_SIZE);

    for (const auto& file : files) {
        writer.beginFile(file.rel_path.string(), chain);

        std::ifstream input(file.abs_path, std::ios::binary);
        if (!input) continue;

        uint64_t bytes_read = 0;
        while (bytes_read < file.file_size) {
            size_t to_read =
                std::min(STREAMING_CHUNK_SIZE,
                         static_cast<size_t>(file.file_size - bytes_read));
            input.read(reinterpret_cast<char*>(buf.data()),
                       static_cast<std::streamsize>(to_read));
            size_t actual = static_cast<size_t>(input.gcount());
            if (actual == 0) break;
            writer.pushFileData(std::span<const uint8_t>(buf.data(), actual));
            bytes_read += actual;
        }

        writer.endFile();

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
    auto comp_u32 = static_cast<uint32_t>(std::min<uint64_t>(total_written, UINT32_MAX));
    wcx::patchCompressedSize(output, comp_u32);
    output.flush();

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

    auto file_reader = std::make_unique<archiver::FileReader>(input_path);
    if (file_reader->size() == 0) {
        result.error_message = "Cannot open archive: " + input_path;
        return result;
    }

    std::vector<uint8_t> archive_data(file_reader->size());
    if (!archive_data.empty()) {
        auto read_n =
            file_reader->read(0, std::span<uint8_t>(archive_data.data(), archive_data.size()));
        if (read_n != archive_data.size()) {
            result.error_message = "Failed to read archive payload";
            return result;
        }
    }

    auto pack_payload_opt =
        resolve_wcx_directory_archive_inner_pack(input_path, archive_data, result);
    if (!pack_payload_opt) {
        return result;
    }
    std::vector<uint8_t> pack_payload = std::move(*pack_payload_opt);

    auto reader = std::make_unique<VectorReader>(pack_payload);
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

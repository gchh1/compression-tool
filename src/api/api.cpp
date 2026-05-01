#include "api.hpp"

#include <algorithm>
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

}  // namespace

// ---- public API ----

auto compress(const std::vector<uint8_t>& data, AlgorithmID algo)
    -> CompressResult {
    CompressResult result;
    result.original_size = data.size();

    std::vector<std::unique_ptr<algorithm::IAlgorithm>> algos;
    if (auto a = core::createAlgorithm(algo)) {
        algos.push_back(std::move(a));
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
    result.compressed_size = result.data.size();
    result.success = true;
    return result;
}

auto decompress(const std::vector<uint8_t>& data, AlgorithmID algo)
    -> CompressResult {
    CompressResult result;
    result.original_size = data.size();

    std::vector<std::unique_ptr<algorithm::IAlgorithm>> algos;
    if (auto a = core::createAlgorithm(algo)) {
        algos.push_back(std::move(a));
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
    result.compressed_size = result.data.size();
    result.success = true;
    return result;
}

auto packAndCompress(const std::vector<WebFile>& files, AlgorithmID algo)
    -> std::vector<uint8_t> {
    auto pool = std::make_shared<memory::MemoryPool>(8, 65536);
    archiver::PackWriter writer(pool);

    std::vector<uint8_t> result;

    for (const auto& f : files) {
        writer.beginFile(f.name, algo);
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

}  // namespace compressor::api

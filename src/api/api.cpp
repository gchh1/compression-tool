#include "api.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <span>
#include <vector>

#include "BackgroundWriter.hpp"
#include "DataChunk.hpp"
#include "DebugLog.hpp"
#include "DiskVizObserver.hpp"
#include "EntropyCollector.hpp"
#include "IAlgorithm.hpp"
#include "MemoryPool.hpp"
#include "Pipeline.hpp"
#include "StreamChunkPolicy.hpp"
#include "WCXProtocol.hpp"
#include "Inflate3HM.hpp"

#ifndef __EMSCRIPTEN__
#include <filesystem>
#include <fstream>
#include "WCXReader.hpp"

namespace fs = std::filesystem;
#endif

namespace compressor::api {

using namespace compressor::algorithm;
using namespace compressor::archiver;
using namespace compressor::archiver::wcx;
using namespace compressor::core;
using namespace compressor::memory;
using namespace compressor::processor;

#ifndef __EMSCRIPTEN__
namespace detail_stream_cancel {
std::atomic<bool> flag{false};
inline bool is_cancel_requested() {
    return flag.load(std::memory_order_relaxed);
}
inline void set_flag(bool v) {
    flag.store(v, std::memory_order_relaxed);
}
}

void set_streaming_compress_cancel_requested(bool requested) {
    detail_stream_cancel::set_flag(requested);
}
#endif

/// Pool chunk count is sized proportionally to input file size, bounded [4, 32].
/// For a 100 KB file this yields 4 chunks (vs. former fixed 32), cutting small-file
/// heap allocation ~8× while keeping enough headroom for burst emission.
constexpr size_t kStreamingPipelinePoolChunksMin = 4;
constexpr size_t kStreamingPipelinePoolChunksMax = 32;

inline size_t adaptivePoolChunks(size_t data_size, size_t chunk_bytes) {
    if (data_size == 0) return kStreamingPipelinePoolChunksMin;
    return std::clamp(
        (data_size + chunk_bytes - 1) / chunk_bytes + 2,
        kStreamingPipelinePoolChunksMin,
        kStreamingPipelinePoolChunksMax);
}
/// If the declared WCX payload is no larger than this, read the full payload before decoding
/// (compressed side is usually small). Decompressed bytes still flow through ``pull()`` in
/// pool-sized chunks and are written incrementally to the staged output file.
constexpr uint64_t kDecompressWholePayloadMaxBytes = 256ULL * 1024 * 1024;

#ifndef __EMSCRIPTEN__
auto staged_part_path(const std::string& final_path) -> fs::path {
    return fs::path(final_path + ".part");
}

void remove_path_best_effort(const fs::path& p) {
    std::error_code ec;
    fs::remove(p, ec);
}

auto commit_staged_to_final(const fs::path& part_path, const fs::path& final_path,
                            std::string& err_out) -> bool {
    std::error_code ec;
    // Try rename first (atomic on same volume)
    fs::rename(part_path, final_path, ec);
    if (!ec) return true;
    // rename failed; try manual copy + remove as fallback
    ec.clear();
    fs::remove(final_path, ec);
    ec.clear();
    std::ifstream src(part_path.string(), std::ios::binary);
    if (!src) {
        err_out = "cannot open source for copy";
        return false;
    }
    std::ofstream dst(final_path.string(), std::ios::binary | std::ios::trunc);
    if (!dst) {
        err_out = "cannot open destination for copy";
        return false;
    }
    dst << src.rdbuf();
    if (!dst.good()) {
        err_out = "copy write failed";
        return false;
    }
    src.close();
    dst.close();
    fs::remove(part_path, ec);
    return true;
}
#endif

#ifndef __EMSCRIPTEN__
struct CancelCallbackRegistrar {
    CancelCallbackRegistrar() {
        compressor::algorithm::g_cancel_callback = detail_stream_cancel::is_cancel_requested;
    }
};
static CancelCallbackRegistrar g_cancel_registrar;
#endif

// ---- public API ----

auto compress(const std::vector<uint8_t>& data,
              std::span<const AlgorithmID> chain,
              const core::LzdpWholeFileParams* lzdp_whole_file,
              const core::DpflatePipelineParams* dpflate_pipeline,
              const core::DeflatePipelineParams* deflate_pipeline,
              const core::ImageCompressParams* image_compress,
              std::size_t streaming_compress_chunk_bytes) -> CompressResult {
    CompressResult result;
    result.original_size = data.size();

    auto t0 = std::chrono::high_resolution_clock::now();

    const size_t chunk =
        processor::effective_stream_chunk_bytes(streaming_compress_chunk_bytes);

    std::vector<std::unique_ptr<algorithm::IAlgorithm>> algos;
    for (auto id : chain) {
        const core::LzdpWholeFileParams* lz =
            (id == core::AlgorithmID::LZDP) ? lzdp_whole_file : nullptr;
        const core::DpflatePipelineParams* df =
            (id == core::AlgorithmID::DPFlate) ? dpflate_pipeline : nullptr;
        const core::DeflatePipelineParams* dfl =
            (id == core::AlgorithmID::Deflate) ? deflate_pipeline : nullptr;
        if (auto a = core::createAlgorithm(id, core::kFileCompressOptsNone, lz, chunk, df, dfl, image_compress))
            algos.push_back(std::move(a));
    }
    if (algos.empty()) {
        result.error_message = "Unknown or null algorithm";
        return result;
    }

    auto pool = std::make_shared<memory::MemoryPool>(
        adaptivePoolChunks(data.size(), chunk), chunk);
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

auto decompress(const std::vector<uint8_t>& data,
                std::span<const AlgorithmID> chain,
                const core::LzdpWholeFileParams* lzdp_whole_file,
                const core::DpflatePipelineParams* dpflate_pipeline,
                const core::DeflatePipelineParams* deflate_pipeline,
                const core::ImageCompressParams* image_compress,
                std::size_t streaming_compress_chunk_bytes) -> CompressResult {
    CompressResult result;
    result.original_size = data.size();

    auto t0 = std::chrono::high_resolution_clock::now();

    const size_t chunk =
        processor::effective_stream_chunk_bytes(streaming_compress_chunk_bytes);

    std::vector<std::unique_ptr<algorithm::IAlgorithm>> algos;
    for (auto id : chain) {
        const core::LzdpWholeFileParams* lz =
            (id == core::AlgorithmID::LZDP) ? lzdp_whole_file : nullptr;
        const core::DpflatePipelineParams* df =
            (id == core::AlgorithmID::DPFlate) ? dpflate_pipeline : nullptr;
        const core::DeflatePipelineParams* dfl =
            (id == core::AlgorithmID::Deflate) ? deflate_pipeline : nullptr;
        if (auto a = core::createAlgorithm(id, core::kFileCompressOptsNone, lz, chunk, df, dfl, image_compress))
            algos.push_back(std::move(a));
    }
    if (algos.empty()) {
        result.error_message = "Unknown or null algorithm";
        return result;
    }

    // memory::MemoryPool: adaptive slots sized to input
    auto pool = std::make_shared<memory::MemoryPool>(
        adaptivePoolChunks(data.size(), chunk), chunk);
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
                  std::span<const AlgorithmID> chain,
                  size_t stream_chunk_bytes,
                  uint32_t file_compress_opts,
                  const core::LzdpWholeFileParams* lzdp_whole_file,
                  const core::DpflatePipelineParams* dpflate_pipeline,
                  const core::DeflatePipelineParams* deflate_pipeline) -> CompressResult {
    CompressResult result;

    auto t0 = std::chrono::high_resolution_clock::now();

    const size_t chunk =
        processor::effective_stream_chunk_bytes(stream_chunk_bytes);

    std::error_code ec;
    result.original_size = fs::file_size(input_path, ec);
    if (ec) {
        result.error_message = "Cannot open input: " + ec.message();
        return result;
    }

    std::vector<std::unique_ptr<algorithm::IAlgorithm>> algos;
    for (auto id : chain) {
        const core::LzdpWholeFileParams* lz =
            (id == core::AlgorithmID::LZDP) ? lzdp_whole_file : nullptr;
        const core::DpflatePipelineParams* df =
            (id == core::AlgorithmID::DPFlate) ? dpflate_pipeline : nullptr;
        const core::DeflatePipelineParams* dfl =
            (id == core::AlgorithmID::Deflate) ? deflate_pipeline : nullptr;
        if (auto a = core::createAlgorithm(id, file_compress_opts, lz, chunk, df,
                                           dfl))
            algos.push_back(std::move(a));
    }
    if (algos.empty()) {
        result.error_message = "Unknown or null algorithm";
        return result;
    }

    auto pool = std::make_shared<memory::MemoryPool>(
        adaptivePoolChunks(static_cast<size_t>(result.original_size), chunk), chunk);
    processor::Pipeline pipeline(std::move(algos), pool);

    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
        result.error_message = "Cannot open input file";
        return result;
    }

    const fs::path path_final(output_path);
    const fs::path path_part = staged_part_path(output_path);
    remove_path_best_effort(path_part);

    // Step 1 — write WCX header synchronously to .part
    {
        std::ofstream output(path_part, std::ios::binary | std::ios::trunc);
        if (!output) {
            result.error_message = "Cannot open staged output file";
            return result;
        }

        std::string original_filename = fs::path(input_path).filename().string();
        uint8_t algo_code = chain.empty() ? 0 : wcx::toAlgoCode(chain.front());
        auto header_orig_u32 =
            static_cast<uint32_t>(std::min<uint64_t>(result.original_size, UINT32_MAX));
        if (!wcx::writeHeader(output, algo_code, header_orig_u32, 0, original_filename)) {
            result.error_message = "Cannot write WCX header";
            output.close();
            remove_path_best_effort(path_part);
            return result;
        }
        output.flush();
        output.close();
    }

    // Step 2 — open BackgroundWriter to append compressed payload
    std::unique_ptr<compressor::viz::BackgroundWriter> writer;
    writer = std::make_unique<compressor::viz::BackgroundWriter>(path_part.string(), true);

    auto abort_compress_file = [&](const std::string& msg) -> CompressResult {
        CompressResult r;
        if (writer) writer->stop();
        input.close();
        remove_path_best_effort(path_part);
        auto t1 = std::chrono::high_resolution_clock::now();
        r.time_ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        r.error_message = msg;
        r.success = false;
        return r;
    };

    std::vector<uint8_t> buf(chunk);
    uint64_t bytes_read = 0;
    uint64_t total_written = 0;

    auto drain_pulls = [&]() {
        while (true) {
            if (detail_stream_cancel::is_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            auto out_chunk = pipeline.pull();
            if (out_chunk.empty()) break;
            auto v = out_chunk.view();
            writer->submit(out_chunk.owner(), v.size());
            total_written += v.size();
        }
    };

    try {
        while (bytes_read < result.original_size) {
            if (detail_stream_cancel::is_cancel_requested()) {
                return abort_compress_file("cancelled");
            }
            size_t to_read = std::min(chunk,
                                      static_cast<size_t>(result.original_size -
                                                          bytes_read));
            input.read(reinterpret_cast<char*>(buf.data()),
                       static_cast<std::streamsize>(to_read));
            size_t actual = static_cast<size_t>(input.gcount());
            if (actual == 0) break;

            bool is_last = (bytes_read + actual >= result.original_size);
            pipeline.push(std::span<const uint8_t>(buf.data(), actual), is_last);
            bytes_read += actual;

            drain_pulls();
        }

        if (bytes_read == 0 && result.original_size == 0) {
            if (detail_stream_cancel::is_cancel_requested()) {
                return abort_compress_file("cancelled");
            }
            pipeline.push(std::span<const uint8_t>{}, true);
            drain_pulls();
        }

        if (detail_stream_cancel::is_cancel_requested()) {
            return abort_compress_file("cancelled");
        }

        pipeline.finish();
        drain_pulls();
    } catch (const std::exception& e) {
        return abort_compress_file(e.what());
    }

    // Step 3 — wait for async writes to complete
    writer->stop();
    writer.reset();

    // Step 4 — patch compressed_size in WCX header at byte offset 10
    {
        auto comp_u32 = static_cast<uint32_t>(std::min<uint64_t>(total_written, UINT32_MAX));
        std::fstream fout(path_part, std::ios::binary | std::ios::in | std::ios::out);
        if (!fout || !wcx::patchCompressedSize(fout, comp_u32)) {
            result.error_message = "Cannot patch WCX header";
            remove_path_best_effort(path_part);
            auto t1 = std::chrono::high_resolution_clock::now();
            result.time_ms =
                std::chrono::duration<double, std::milli>(t1 - t0).count();
            return result;
        }
        fout.flush();
        fout.close();
    }

    std::string commit_err;
    if (!commit_staged_to_final(path_part, path_final, commit_err)) {
        result.error_message = "Cannot commit output: " + commit_err;
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

auto decompressFile(const std::string& input_path,
                    const std::string& output_path,
                    std::span<const AlgorithmID> chain,
                    size_t stream_chunk_bytes) -> CompressResult {
    CompressResult result;

    auto t0 = std::chrono::high_resolution_clock::now();
    const size_t chunk =
        processor::effective_stream_chunk_bytes(stream_chunk_bytes);

    std::error_code ec;
    result.original_size = fs::file_size(input_path, ec);
    if (ec) {
        result.error_message = "Cannot open input: " + ec.message();
        return result;
    }

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

    uint8_t payload_lead_byte = 0;
    if (payload_size > 0) {
        char lead{};
        input.read(&lead, 1);
        payload_lead_byte = static_cast<uint8_t>(lead);
        input.seekg(static_cast<std::streamoff>(payload_begin));
    }
    const bool payload_is_3hm = (payload_lead_byte == 0x33);

    std::vector<std::unique_ptr<algorithm::IAlgorithm>> algos;
    for (auto id : chain) {
        if (id == core::AlgorithmID::Inflate && payload_is_3hm) {
            algos.push_back(std::make_unique<algorithm::Inflate3HM>());
            continue;
        }
        if (auto a = core::createAlgorithm(id, core::kFileCompressOptsNone, nullptr, 0)) {
            algos.push_back(std::move(a));
        }
    }
    if (algos.empty()) {
        result.error_message = "Unknown or null algorithm";
        return result;
    }

    auto pool = std::make_shared<memory::MemoryPool>(
        adaptivePoolChunks(static_cast<size_t>(result.original_size), chunk), chunk);
    processor::Pipeline pipeline(std::move(algos), pool);

    if (const char* ev = std::getenv("WEBCOMPRESS_DECOMPRESS_DEBUG")) {
        if (ev[0] != '\0' && ev[0] != '0') {
            const bool whole_in = (payload_size <= kDecompressWholePayloadMaxBytes);
            std::fprintf(stderr,
                         "[decompress][native] begin input=%s output=%s "
                         "file_on_disk=%llu payload_bytes=%llu payload_begin=%llu "
                         "declared_original=%llu pool_chunk=%zu mode=%s\n",
                         input_path.c_str(), output_path.c_str(),
                         static_cast<unsigned long long>(file_on_disk),
                         static_cast<unsigned long long>(payload_size),
                         static_cast<unsigned long long>(payload_begin),
                         static_cast<unsigned long long>(result.original_size),
                         chunk, whole_in ? "whole_payload" : "stream_payload");
            std::fflush(stderr);
        }
    }

    const fs::path path_final(output_path);
    const fs::path path_part = staged_part_path(output_path);
    remove_path_best_effort(path_part);

    std::ofstream output(path_part, std::ios::binary | std::ios::trunc);
    if (!output) {
        result.error_message = "Cannot open staged output file";
        return result;
    }

    auto abort_decompress_file = [&](const std::string& msg) -> CompressResult {
        if (const char* ev = std::getenv("WEBCOMPRESS_DECOMPRESS_DEBUG")) {
            if (ev[0] != '\0' && ev[0] != '0') {
                std::fprintf(stderr, "[decompress][native] abort msg=%s\n", msg.c_str());
                std::fflush(stderr);
            }
        }
        CompressResult r;
        input.close();
        output.close();
        remove_path_best_effort(path_part);
        auto t1 = std::chrono::high_resolution_clock::now();
        r.time_ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        r.error_message = msg;
        r.success = false;
        return r;
    };

    std::vector<uint8_t> buf(chunk);
    uint64_t bytes_read = 0;
    uint64_t total_written = 0;

    auto drain_pulls = [&]() {
        while (true) {
            if (detail_stream_cancel::is_cancel_requested()) {
                throw std::runtime_error("cancelled");
            }
            auto out_chunk = pipeline.pull();
            if (out_chunk.empty()) break;
            auto v = out_chunk.view();
            output.write(reinterpret_cast<const char*>(v.data()),
                         static_cast<std::streamsize>(v.size()));
            total_written += v.size();
        }
    };

    try {
        if (payload_size <= kDecompressWholePayloadMaxBytes) {
            std::vector<uint8_t> payload(static_cast<size_t>(payload_size));
            if (payload_size > 0) {
                input.read(reinterpret_cast<char*>(payload.data()),
                             static_cast<std::streamsize>(payload_size));
                const auto got = static_cast<uint64_t>(input.gcount());
                if (got != payload_size) {
                    result.error_message = "WCX payload truncated";
                    output.close();
                    remove_path_best_effort(path_part);
                    auto t1 = std::chrono::high_resolution_clock::now();
                    result.time_ms =
                        std::chrono::duration<double, std::milli>(t1 - t0).count();
                    return result;
                }
            }
            bytes_read = payload_size;
            // Strip DPFlate format byte (0x46=FLATE, 0x33=3HfMT) if present;
            // the Inflate algorithm expects a raw Deflate bitstream.
            size_t payload_offset = 0;
            if (payload_size > 0) {
                const uint8_t fmt = payload[0];
                if (fmt == 0x46 || fmt == 0x33) {
                    payload_offset = 1;
                }
            }
            pipeline.push(std::span<const uint8_t>(payload.data() + payload_offset,
                                                   payload.size() - payload_offset), true);
            drain_pulls();
            pipeline.finish();
            drain_pulls();
        } else {
            bool fmt_byte_skipped = false;
            while (bytes_read < payload_size) {
                if (detail_stream_cancel::is_cancel_requested()) {
                    return abort_decompress_file("cancelled");
                }
                size_t to_read = std::min(chunk,
                                          static_cast<size_t>(payload_size -
                                                              bytes_read));
                input.read(reinterpret_cast<char*>(buf.data()),
                           static_cast<std::streamsize>(to_read));
                size_t actual = static_cast<size_t>(input.gcount());
                if (actual == 0) break;

                size_t push_offset = 0;
                size_t push_size = actual;
                if (!fmt_byte_skipped && actual > 0) {
                    const uint8_t fmt = buf[0];
                    if (fmt == 0x46 || fmt == 0x33) {
                        push_offset = 1;
                        push_size = actual - 1;
                    }
                    fmt_byte_skipped = true;
                }

                bool is_last = (bytes_read + actual >= payload_size);
                if (push_size > 0) {
                    pipeline.push(std::span<const uint8_t>(buf.data() + push_offset, push_size), is_last);
                } else if (is_last) {
                    pipeline.push(std::span<const uint8_t>{}, true);
                }
                bytes_read += actual;

                drain_pulls();
            }

            pipeline.finish();
            drain_pulls();
        }
    } catch (const std::exception& e) {
        return abort_decompress_file(e.what());
    }

    if (bytes_read != payload_size) {
        result.error_message = "WCX payload truncated";
        output.close();
        remove_path_best_effort(path_part);
        auto t1 = std::chrono::high_resolution_clock::now();
        result.time_ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        return result;
    }

    output.flush();
    if (!output.good()) {
        result.error_message = "Cannot flush staged output file";
        output.close();
        remove_path_best_effort(path_part);
        auto t1 = std::chrono::high_resolution_clock::now();
        result.time_ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        return result;
    }
    output.close();
    std::string commit_err;
    if (!commit_staged_to_final(path_part, path_final, commit_err)) {
        result.error_message = "Cannot commit output: " + commit_err;
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
    if (const char* ev = std::getenv("WEBCOMPRESS_DECOMPRESS_DEBUG")) {
        if (ev[0] != '\0' && ev[0] != '0') {
            std::fprintf(stderr,
                         "[decompress][native] ok written=%llu declared_original=%llu "
                         "ratio=%.6f time_ms=%.2f\n",
                         static_cast<unsigned long long>(total_written),
                         static_cast<unsigned long long>(result.original_size),
                         result.compression_ratio, result.time_ms);
            if (result.original_size > 0 &&
                total_written != static_cast<uint64_t>(result.original_size)) {
                std::fprintf(stderr,
                             "[decompress][native] WARN written != declared_original_size\n");
            }
            std::fflush(stderr);
        }
    }
    return result;
}

auto compressFileWithViz(const std::string& input_path,
                         const std::string& output_path,
                         const std::string& viz_path,
                         std::span<const AlgorithmID> chain,
                         size_t stream_chunk_bytes,
                         const std::string& heat_path) -> CompressResult {
    CompressResult result;

    auto t0 = std::chrono::high_resolution_clock::now();

    const size_t chunk =
        processor::effective_stream_chunk_bytes(stream_chunk_bytes);

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

    auto viz_observer =
        std::make_shared<compressor::viz::DiskVizObserver>(viz_path);
    for (auto& algo : algos) {
        auto* base = dynamic_cast<algorithm::AlgorithmBase*>(algo.get());
        if (base) {
            base->attachObserver(viz_observer.get());
            break;
        }
    }

    // Entropy collector (optional — same pass as compression)
    std::shared_ptr<compressor::viz::EntropyCollector> entropy_collector;
    if (!heat_path.empty()) {
        entropy_collector =
            std::make_shared<compressor::viz::EntropyCollector>(heat_path, static_cast<uint32_t>(chunk));
    }

    auto pool = std::make_shared<memory::MemoryPool>(
        adaptivePoolChunks(static_cast<size_t>(result.original_size), chunk), chunk);
    processor::Pipeline pipeline(std::move(algos), pool);

    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
        result.error_message = "Cannot open input file";
        return result;
    }

    const fs::path path_final(output_path);
    const fs::path path_part = staged_part_path(output_path);
    remove_path_best_effort(path_part);

    // Step 1 — write WCX header synchronously to .part
    {
        std::ofstream output(path_part, std::ios::binary | std::ios::trunc);
        if (!output) {
            result.error_message = "Cannot open staged output file";
            return result;
        }

        std::string original_filename = fs::path(input_path).filename().string();
        uint8_t algo_code = chain.empty() ? 0 : wcx::toAlgoCode(chain.front());
        auto header_orig_u32 =
            static_cast<uint32_t>(std::min<uint64_t>(result.original_size, UINT32_MAX));
        if (!wcx::writeHeader(output, algo_code, header_orig_u32, 0, original_filename)) {
            result.error_message = "Cannot write WCX header";
            output.close();
            remove_path_best_effort(path_part);
            return result;
        }
        output.flush();
        output.close();
    }

    // Step 2 — open BackgroundWriter to append compressed payload
    auto writer = std::make_unique<compressor::viz::BackgroundWriter>(path_part.string(), true);

    std::vector<uint8_t> buf(chunk);
    uint64_t bytes_read = 0;
    uint64_t total_written = 0;

    while (bytes_read < result.original_size) {
        size_t to_read = std::min(chunk,
                                  static_cast<size_t>(result.original_size -
                                                      bytes_read));
        input.read(reinterpret_cast<char*>(buf.data()),
                   static_cast<std::streamsize>(to_read));
        size_t actual = static_cast<size_t>(input.gcount());
        if (actual == 0) break;

        // Compute entropy on raw chunk BEFORE compression
        if (entropy_collector)
            entropy_collector->onRawChunk(buf.data(), actual);

        bool is_last = (bytes_read + actual >= result.original_size);
        pipeline.push(std::span<const uint8_t>(buf.data(), actual), is_last);
        bytes_read += actual;

        while (true) {
            auto out_chunk = pipeline.pull();
            if (out_chunk.empty()) break;
            auto v = out_chunk.view();
            writer->submit(out_chunk.owner(), v.size());
            total_written += v.size();
        }
    }

    pipeline.finish();

    while (true) {
        auto out_chunk = pipeline.pull();
        if (out_chunk.empty()) break;
        auto v = out_chunk.view();
        writer->submit(out_chunk.owner(), v.size());
        total_written += v.size();
    }

    // Step 3 — flush entropy collector
    if (entropy_collector) {
        entropy_collector->onCompressionFinish();
        entropy_collector.reset();
    }

    // Step 4 — viz observer MUST be reset before writer->stop(), so viz files
    // (written by independent BackgroundWriter threads) are flushed first.
    viz_observer.reset();

    // Step 5 — wait for async writes to complete
    writer->stop();
    writer.reset();

    // Step 6 — patch compressed_size in WCX header at byte offset 10
    {
        auto comp_u32 = static_cast<uint32_t>(std::min<uint64_t>(total_written, UINT32_MAX));
        std::fstream fout(path_part, std::ios::binary | std::ios::in | std::ios::out);
        if (!fout || !wcx::patchCompressedSize(fout, comp_u32)) {
            result.error_message = "Cannot patch WCX header";
            remove_path_best_effort(path_part);
            auto t1 = std::chrono::high_resolution_clock::now();
            result.time_ms =
                std::chrono::duration<double, std::milli>(t1 - t0).count();
            return result;
        }
        fout.flush();
        fout.close();
    }

    std::string commit_err;
    if (!commit_staged_to_final(path_part, path_final, commit_err)) {
        result.error_message = "Cannot commit output: " + commit_err;
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

auto compressFileWithHeat(const std::string& input_path,
                          const std::string& output_path,
                          const std::string& heat_path,
                          std::span<const AlgorithmID> chain,
                          size_t stream_chunk_bytes) -> CompressResult {
    CompressResult result;

    auto t0 = std::chrono::high_resolution_clock::now();

    const size_t chunk =
        processor::effective_stream_chunk_bytes(stream_chunk_bytes);

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

    auto entropy_collector =
        std::make_shared<compressor::viz::EntropyCollector>(heat_path, static_cast<uint32_t>(chunk));

    auto pool = std::make_shared<memory::MemoryPool>(
        adaptivePoolChunks(static_cast<size_t>(result.original_size), chunk), chunk);
    processor::Pipeline pipeline(std::move(algos), pool);

    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
        result.error_message = "Cannot open input file";
        return result;
    }

    const fs::path path_final(output_path);
    const fs::path path_part = staged_part_path(output_path);
    remove_path_best_effort(path_part);

    // Write WCX header
    {
        std::ofstream output(path_part, std::ios::binary | std::ios::trunc);
        if (!output) {
            result.error_message = "Cannot open staged output file";
            return result;
        }
        std::string original_filename = fs::path(input_path).filename().string();
        uint8_t algo_code = chain.empty() ? 0 : wcx::toAlgoCode(chain.front());
        auto header_orig_u32 =
            static_cast<uint32_t>(std::min<uint64_t>(result.original_size, UINT32_MAX));
        if (!wcx::writeHeader(output, algo_code, header_orig_u32, 0, original_filename)) {
            result.error_message = "Cannot write WCX header";
            output.close();
            remove_path_best_effort(path_part);
            return result;
        }
        output.flush();
        output.close();
    }

    auto writer = std::make_unique<compressor::viz::BackgroundWriter>(path_part.string(), true);

    std::vector<uint8_t> buf(chunk);
    uint64_t bytes_read = 0;
    uint64_t total_written = 0;

    while (bytes_read < result.original_size) {
        size_t to_read = std::min(chunk,
                                  static_cast<size_t>(result.original_size - bytes_read));
        input.read(reinterpret_cast<char*>(buf.data()),
                   static_cast<std::streamsize>(to_read));
        size_t actual = static_cast<size_t>(input.gcount());
        if (actual == 0) break;

        entropy_collector->onRawChunk(buf.data(), actual);

        bool is_last = (bytes_read + actual >= result.original_size);
        pipeline.push(std::span<const uint8_t>(buf.data(), actual), is_last);
        bytes_read += actual;

        while (true) {
            auto out_chunk = pipeline.pull();
            if (out_chunk.empty()) break;
            auto v = out_chunk.view();
            writer->submit(out_chunk.owner(), v.size());
            total_written += v.size();
        }
    }

    pipeline.finish();

    while (true) {
        auto out_chunk = pipeline.pull();
        if (out_chunk.empty()) break;
        auto v = out_chunk.view();
        writer->submit(out_chunk.owner(), v.size());
        total_written += v.size();
    }

    entropy_collector->onCompressionFinish();
    entropy_collector.reset();

    writer->stop();
    writer.reset();

    // Patch compressed_size in WCX header
    {
        auto comp_u32 = static_cast<uint32_t>(std::min<uint64_t>(total_written, UINT32_MAX));
        std::fstream fout(path_part, std::ios::binary | std::ios::in | std::ios::out);
        if (!fout || !wcx::patchCompressedSize(fout, comp_u32)) {
            result.error_message = "Cannot patch WCX header";
            remove_path_best_effort(path_part);
            auto t1 = std::chrono::high_resolution_clock::now();
            result.time_ms =
                std::chrono::duration<double, std::milli>(t1 - t0).count();
            return result;
        }
        fout.flush();
        fout.close();
    }

    std::string commit_err;
    if (!commit_staged_to_final(path_part, path_final, commit_err)) {
        result.error_message = "Cannot commit output: " + commit_err;
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

auto scanWcx(const std::string& input_path) -> std::vector<WcxEntrySummary> {
    std::vector<WcxEntrySummary> entries;
    std::ifstream in(input_path, std::ios::binary);
    if (!in) return entries;

    while (in) {
        wcx::HeaderView hdr;
        if (!wcx::tryReadHeader(in, hdr) || !hdr.valid) break;
        entries.push_back(WcxEntrySummary{
            hdr.original_filename,
            hdr.original_size,
            hdr.compressed_size,
            hdr.algo_code,
        });
        // Skip compressed payload
        in.seekg(static_cast<std::streamoff>(hdr.compressed_size), std::ios::cur);
    }
    return entries;
}

#endif  // __EMSCRIPTEN__

}  // namespace compressor::api

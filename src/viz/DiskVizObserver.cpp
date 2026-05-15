#include "DiskVizObserver.hpp"

#include "MemoryPool.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>

namespace compressor::viz {

DiskVizObserver::DiskVizObserver(const std::string& path)
    : path_(path) {}

DiskVizObserver::~DiskVizObserver() {
    // Stop writers and clean up temp files
    for (uint32_t i = 0; i < kNumEventTypes; ++i) {
        writers_[i].reset();  // calls stop() + join
        if (!tmp_paths_[i].empty())
            std::remove(tmp_paths_[i].c_str());
    }
}

void DiskVizObserver::ensureStarted() {
    if (started_) return;
    started_ = true;

    // 8 chunks × 64 KB = 512 KB pool; main thread holds 4, leaves 4 for queue
    pool_ = std::make_shared<compressor::memory::MemoryPool>(8, kChunkSize);

    for (uint32_t i = 0; i < kNumEventTypes; ++i) {
        tmp_paths_[i] = path_ + ".part" + std::to_string(i);
        writers_[i] = std::make_unique<BackgroundWriter>(tmp_paths_[i]);
        bufs_[i] = pool_->acquire();
    }
}

void DiskVizObserver::flushType(uint32_t type_idx) {
    if (buf_offsets_[type_idx] == 0) return;

    // Move shared_ptr to background thread → it writes, then deleter returns to pool
    writers_[type_idx]->submit(std::move(bufs_[type_idx]), buf_offsets_[type_idx]);

    // Acquire fresh buffer from pool (blocks iff pool exhausted = natural back-pressure)
    bufs_[type_idx] = pool_->acquire();
    buf_offsets_[type_idx] = 0;
}

void DiskVizObserver::onEvent(VizEvent event) {
    ensureStarted();

    uint8_t tmp[kMaxPayloadSize];
    size_t n = serializeEventPayload(event, tmp);

    uint32_t type_idx = static_cast<uint32_t>(event.index());
    auto& off = buf_offsets_[type_idx];

    if (off + n > kChunkSize)
        flushType(type_idx);

    std::memcpy(bufs_[type_idx]->data() + off, tmp, n);
    off += n;
    type_sizes_[type_idx] += n;
    ++event_counts_[type_idx];
    ++total_events_;
}

void DiskVizObserver::onBlockFinish() {}

void DiskVizObserver::onCompressionFinish() {
    // Flush remaining buffers + stop all writers
    for (uint32_t i = 0; i < kNumEventTypes; ++i) {
        flushType(i);
        writers_[i]->stop();
    }

    // ── Compute layout ──────────────────────────────────────────
    static constexpr uint64_t kHeaderSize = 8;
    static constexpr uint64_t kTableSize  = kNumEventTypes * (4 + 8 + 8);

    uint64_t section_offsets[kNumEventTypes];
    uint64_t data_start = kHeaderSize + kTableSize;
    section_offsets[0] = data_start;
    for (uint32_t i = 1; i < kNumEventTypes; ++i)
        section_offsets[i] = section_offsets[i - 1] + type_sizes_[i - 1];

    // ── Write final .viz ─────────────────────────────────────────
    std::ofstream f(path_, std::ios::binary | std::ios::trunc);

    uint32_t magic = kMagic;
    uint32_t version = kVersion;
    f.write(reinterpret_cast<const char*>(&magic), 4);
    f.write(reinterpret_cast<const char*>(&version), 4);

    for (uint32_t i = 0; i < kNumEventTypes; ++i) {
        f.write(reinterpret_cast<const char*>(&i), 4);
        f.write(reinterpret_cast<const char*>(&section_offsets[i]), 8);
        f.write(reinterpret_cast<const char*>(&type_sizes_[i]), 8);
    }

    constexpr size_t kCopyBufSize = 256 * 1024;
    auto copy_buf = std::make_unique<char[]>(kCopyBufSize);

    for (uint32_t i = 0; i < kNumEventTypes; ++i) {
        if (type_sizes_[i] == 0) continue;

        std::ifstream in(tmp_paths_[i], std::ios::binary);
        while (in) {
            in.read(copy_buf.get(), kCopyBufSize);
            std::streamsize n = in.gcount();
            if (n > 0)
                f.write(copy_buf.get(), n);
        }
        in.close();
        std::remove(tmp_paths_[i].c_str());
        tmp_paths_[i].clear();
    }

    uint64_t footer_start = static_cast<uint64_t>(f.tellp());
    f.write(reinterpret_cast<const char*>(&total_events_), 4);
    f.write(reinterpret_cast<const char*>(&footer_start), 8);
}

}  // namespace compressor::viz

#include "DiskVizObserver.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>

namespace compressor::viz {

DiskVizObserver::DiskVizObserver(const std::string& path)
    : path_(path) {}

DiskVizObserver::~DiskVizObserver() {
    for (uint32_t i = 0; i < kNumEventTypes; ++i) {
        if (streams_[i].is_open())
            streams_[i].close();
        if (!tmp_paths_[i].empty())
            std::remove(tmp_paths_[i].c_str());
    }
}

void DiskVizObserver::ensureStarted() {
    if (started_) return;
    started_ = true;

    for (uint32_t i = 0; i < kNumEventTypes; ++i) {
        tmp_paths_[i] = path_ + ".part" + std::to_string(i);
        bufs_[i].resize(kChunkSize);
        streams_[i].open(tmp_paths_[i], std::ios::binary | std::ios::trunc);
    }
}

void DiskVizObserver::flushType(uint32_t type_idx) {
    if (buf_offsets_[type_idx] == 0) return;
    streams_[type_idx].write(
        reinterpret_cast<const char*>(bufs_[type_idx].data()),
        static_cast<std::streamsize>(buf_offsets_[type_idx]));
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

    std::memcpy(bufs_[type_idx].data() + off, tmp, n);
    off += n;
    type_sizes_[type_idx] += n;
    ++event_counts_[type_idx];
    ++total_events_;
}

void DiskVizObserver::onBlockFinish() {}

void DiskVizObserver::onCompressionFinish() {
    // Flush remaining buffers and close all per-type streams
    for (uint32_t i = 0; i < kNumEventTypes; ++i) {
        flushType(i);
        streams_[i].close();
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

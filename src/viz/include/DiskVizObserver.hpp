#pragma once

#include "VizEvent.hpp"
#include "BackgroundWriter.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace compressor::memory { class MemoryPool; }

namespace compressor::viz {

/// Async .viz v2 writer backed by MemoryPool + BackgroundWriter.
///
/// Each of the 4 event types gets a MemoryPool buffer.  When full, the
/// buffer is submitted to a BackgroundWriter thread — the main thread
/// never blocks on disk I/O.
class DiskVizObserver : public IVizObserver {
public:
    explicit DiskVizObserver(const std::string& path);
    ~DiskVizObserver() override;

    DiskVizObserver(const DiskVizObserver&) = delete;
    DiskVizObserver& operator=(const DiskVizObserver&) = delete;

    void onEvent(VizEvent event) override;
    void onBlockFinish() override;
    void onCompressionFinish() override;

private:
    static constexpr uint32_t kMagic = 0x305A4956;
    static constexpr uint32_t kVersion = 2;
    static constexpr uint32_t kNumEventTypes = 4;
    static constexpr size_t kMaxPayloadSize = 319;
    static constexpr size_t kChunkSize = 65536;  // 64 KB

    void ensureStarted();
    void flushType(uint32_t type_idx);

    std::string path_;
    std::string tmp_paths_[kNumEventTypes];

    // One background writer per event type → separate temp file
    std::unique_ptr<BackgroundWriter> writers_[kNumEventTypes];

    // Pool-backed buffers (one per type)
    std::shared_ptr<compressor::memory::MemoryPool> pool_;
    std::shared_ptr<std::vector<uint8_t>> bufs_[kNumEventTypes];
    size_t buf_offsets_[kNumEventTypes] = {};

    uint64_t type_sizes_[kNumEventTypes] = {};
    uint32_t event_counts_[kNumEventTypes] = {};
    uint32_t total_events_ = 0;
    bool started_ = false;
};

}  // namespace compressor::viz

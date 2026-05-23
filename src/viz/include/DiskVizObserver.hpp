#pragma once

#include "VizEvent.hpp"

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace compressor::viz {

/// Synchronous .viz v2 writer.
///
/// Events are buffered per type in reusable 64 KB vectors and flushed to
/// temp files when full.  Final assembly in ``onCompressionFinish()``
/// merges the per-type temp files into a single .viz file with a table of
/// contents and footer.
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
    static constexpr uint32_t kNumEventTypes = 5;
    static constexpr size_t kMaxPayloadSize = 319;
    static constexpr size_t kChunkSize = 65536;  // 64 KB

    void ensureStarted();
    void flushType(uint32_t type_idx);

    std::string path_;
    std::string tmp_paths_[kNumEventTypes];
    std::ofstream streams_[kNumEventTypes];

    // Reusable per-type buffers (one vector per event type, 64 KB each)
    std::vector<uint8_t> bufs_[kNumEventTypes];
    size_t buf_offsets_[kNumEventTypes] = {};

    uint64_t type_sizes_[kNumEventTypes] = {};
    uint32_t event_counts_[kNumEventTypes] = {};
    uint32_t total_events_ = 0;
    bool started_ = false;
};

}  // namespace compressor::viz

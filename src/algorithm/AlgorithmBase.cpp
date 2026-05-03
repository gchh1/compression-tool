
#include <cstdint>
#include <span>

#include "IAlgorithm.hpp"

namespace compressor::algorithm {

auto AlgorithmBase::process(std::span<const uint8_t> read,
                            std::span<uint8_t> write, bool is_last_chunk)
    -> AlgorithmStatus {
    // Snapshot cumulative byte counts before changing source
    size_t prev_bytes_read = reader_.getByteRead();

    // Always update source even if empty, so the reader/writer don't
    // hold dangling pointers from a previous call.
    reader_.changeSource(read);
    if (write.size() > 0) {
        writer_.changeSource(write);
    }

    AlgorithmStatus status;

    handle(status, is_last_chunk);
    // Return INCREMENTAL bytes consumed (not cumulative), since callers
    // use this to advance input offsets.
    status.bytes_consumed =
        read.size() > 0 ? reader_.getByteRead() - prev_bytes_read : 0;
    status.bytes_produced = writer_.getBytesWritten();

    return status;
}

}  // namespace compressor::algorithm
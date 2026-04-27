
#include <cstdint>
#include <span>

#include "IAlgorithm.hpp"

namespace compressor::algorithm {

auto AlgorithmBase::process(std::span<const uint8_t> read,
                            std::span<uint8_t> write, bool is_last_chunk)
    -> AlgorithmStatus {
    if (read.size() > 0) {
        reader_.changeSource(read);
    }
    if (write.size() > 0) {
        writer_.changeSource(write);
    }

    AlgorithmStatus status;

    handle(status, is_last_chunk);
    status.bytes_consumed = reader_.getByteRead();
    status.bytes_produced = writer_.getBytesWritten();

    return status;
}

}  // namespace compressor::algorithm
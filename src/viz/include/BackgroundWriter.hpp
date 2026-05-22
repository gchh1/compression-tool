#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace compressor::viz {

/// Single-file background writer thread.
///
/// Submits are non-blocking (move shared_ptr + notify).  The worker thread
/// writes to the file; when a WriteTask is destroyed the shared_ptr resets
/// and MemoryPool's custom deleter returns the buffer.
///
/// An append-mode constructor is provided for cases where the file already
/// contains a header or prefix written by the caller.
class BackgroundWriter {
public:
    struct WriteTask {
        std::shared_ptr<const std::vector<uint8_t>> buf;
        size_t size;
    };

    /// Open ``path`` for writing (truncate).
    explicit BackgroundWriter(const std::string& path);

    /// Open ``path`` in append mode — caller has already written a prefix.
    BackgroundWriter(const std::string& path, bool append);

    ~BackgroundWriter();

    BackgroundWriter(const BackgroundWriter&) = delete;
    BackgroundWriter& operator=(const BackgroundWriter&) = delete;

    void submit(std::shared_ptr<const std::vector<uint8_t>> buf, size_t size);
    void stop();

private:
    void workerLoop();

    std::string path_;
    std::ofstream file_;
    bool append_{false};

    // Synchronization primitives — must be initialized before worker_
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<WriteTask> queue_;
    std::atomic<bool> running_{true};

    std::thread worker_;
};

}  // namespace compressor::viz

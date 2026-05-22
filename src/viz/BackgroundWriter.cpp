#include "BackgroundWriter.hpp"

namespace compressor::viz {

BackgroundWriter::BackgroundWriter(const std::string& path)
    : path_(path)
    , file_(path, std::ios::binary | std::ios::trunc)
    , worker_(&BackgroundWriter::workerLoop, this) {}

BackgroundWriter::BackgroundWriter(const std::string& path, bool /*append*/)
    : path_(path)
    , file_(path, std::ios::binary | std::ios::app)
    , append_(true)
    , worker_(&BackgroundWriter::workerLoop, this) {}

BackgroundWriter::~BackgroundWriter() {
    stop();
}

void BackgroundWriter::submit(std::shared_ptr<const std::vector<uint8_t>> buf,
                              size_t size) {
    {
        std::lock_guard lock(mutex_);
        queue_.push_back({std::move(buf), size});
    }
    cv_.notify_one();
}

void BackgroundWriter::stop() {
    if (!running_.exchange(false)) return;  // already stopped

    cv_.notify_all();
    if (worker_.joinable())
        worker_.join();

    // Drain any remaining tasks (should be none after worker exits)
    file_.flush();
    file_.close();
}

void BackgroundWriter::workerLoop() {
    while (running_) {
        WriteTask task;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return !queue_.empty() || !running_; });

            if (!running_ && queue_.empty()) break;

            task = std::move(queue_.front());
            queue_.pop_front();
        }

        if (task.buf && task.size > 0) {
            file_.write(reinterpret_cast<const char*>(task.buf->data()),
                        static_cast<std::streamsize>(task.size));
        }
        // task destructor: shared_ptr reset → MemoryPool deleter returns buffer
    }

    // Drain remaining queue
    while (true) {
        WriteTask task;
        {
            std::lock_guard lock(mutex_);
            if (queue_.empty()) break;
            task = std::move(queue_.front());
            queue_.pop_front();
        }
        if (task.buf && task.size > 0) {
            file_.write(reinterpret_cast<const char*>(task.buf->data()),
                        static_cast<std::streamsize>(task.size));
        }
    }
}

}  // namespace compressor::viz

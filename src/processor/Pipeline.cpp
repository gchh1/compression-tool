#include "Pipeline.hpp"

#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace compressor::processor {

Pipeline::Pipeline(std::vector<std::unique_ptr<IAlgorithm>> algorithms,
                   std::shared_ptr<memory::MemoryPool> pool) {
    for (auto& algo : algorithms) {
        if (algo) {
            stages_.push_back(
                std::make_unique<StreamProcessor>(std::move(algo), pool));
        }
    }
}

auto Pipeline::psh(memory::DataChunk chunk, bool is_last) -> void {
    if (finished_ || stages_.empty()) return;
    stages_.front()->push(std::move(chunk), is_last);
    drainAll();
}

auto Pipeline::push(std::span<const uint8_t> data, bool is_last) -> void {
    if (finished_ || stages_.empty()) return;
    stages_.front()->push(data, is_last);
    drainAll();
}

auto Pipeline::pull() -> memory::DataChunk {
    if (stages_.empty()) return {};
    return stages_.back()->pull();
}

auto Pipeline::consume(size_t n) -> void {
    if (!stages_.empty()) stages_.back()->consume(n);
}

auto Pipeline::finish() -> void {
    if (finished_) return;
    if (!stages_.empty()) {
        stages_.front()->finish();
        drainAll();
        for (size_t i = 1; i < stages_.size(); ++i) {
            stages_[i]->finish();
        }
    }
    finished_ = true;
}

auto Pipeline::isFinished() const -> bool { return finished_; }

auto Pipeline::drainAll() -> void {
    for (size_t i = 0; i + 1 < stages_.size(); ++i) {
        drain(*stages_[i], *stages_[i + 1]);
    }
}

}  // namespace compressor::processor

#include "Pipeline.hpp"

#include <utility>

namespace compressor::processor {

Pipeline::Pipeline(std::unique_ptr<IAlgorithm> first,
                   std::unique_ptr<IAlgorithm> second)
    : first_(std::make_unique<StreamProcessor>(std::move(first))) {
    if (second) {
        second_ = std::make_unique<StreamProcessor>(std::move(second));
    }
}

auto Pipeline::push(std::span<const uint8_t> data, bool is_last) -> void {
    if (finished_) return;
    first_->push(data, is_last);
    drainInternal();
}

auto Pipeline::pull(void) -> std::span<const uint8_t> {
    if (second_) return second_->pull();
    return first_->pull();
}

auto Pipeline::consume(size_t n) -> void {
    if (second_)
        second_->consume(n);
    else
        first_->consume(n);
}

auto Pipeline::finish(void) -> void {
    if (finished_) return;
    first_->finish();
    drainInternal();
    if (second_) {
        second_->finish();
    }
    finished_ = true;
}

auto Pipeline::isFinished(void) const -> bool { return finished_; }

auto Pipeline::drainInternal(void) -> void {
    if (second_) {
        drain(*first_, *second_);
    }
}

}  // namespace compressor::processor

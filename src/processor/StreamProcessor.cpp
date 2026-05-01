#include "StreamProcessor.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace compressor::processor {

// ---- public ----

auto StreamProcessor::push(memory::DataChunk chunk, bool is_last) -> void {
    if (finished_) return;
    if (!chunk.empty()) {
        in_chunks_.push_back(std::move(chunk));
    }
    processChunks(is_last);
}

auto StreamProcessor::push(std::span<const uint8_t> data, bool is_last)
    -> void {
    if (finished_) return;
    if (data.empty()) {
        if (is_last) processChunks(true);
        return;
    }
    auto v = std::make_shared<std::vector<uint8_t>>(data.begin(), data.end());
    push(memory::DataChunk::adopt(v, v->size()), is_last);
}

auto StreamProcessor::pull() -> memory::DataChunk {
    if (ready_chunks_.empty()) return {};
    auto chunk = std::move(ready_chunks_.front());
    ready_chunks_.pop_front();
    return chunk;
}

auto StreamProcessor::consume(size_t n) -> void {
    while (n > 0 && !ready_chunks_.empty()) {
        auto& front = ready_chunks_.front();
        if (n >= front.size()) {
            n -= front.size();
            ready_chunks_.pop_front();
        } else {
            ready_chunks_.front() = front.slice(n, front.size() - n);
            n = 0;
        }
    }
}

auto StreamProcessor::finish() -> std::vector<uint8_t> {
    if (!finished_) {
        processChunks(true);
        publishCurrent();
    }
    // Leave output in ready_chunks_ for pull(); return empty for compat.
    return {};
}

// ---- private ----

auto StreamProcessor::readSpan() const -> std::span<const uint8_t> {
    if (in_chunks_.empty()) return {};
    return in_chunks_.front().view().subspan(in_pos_);
}

auto StreamProcessor::consumeInput(size_t n) -> void {
    in_pos_ += n;
    while (!in_chunks_.empty() && in_pos_ >= in_chunks_.front().size()) {
        in_pos_ -= in_chunks_.front().size();
        in_chunks_.pop_front();
    }
}

auto StreamProcessor::publishCurrent() -> void {
    if (!current_out_ || out_pos_ == 0) return;
    ready_chunks_.push_back(
        memory::DataChunk::adopt(current_out_, out_pos_));
    current_out_.reset();
    out_pos_ = 0;
}

auto StreamProcessor::processChunks(bool is_last) -> void {
    // ---- passthrough: no algorithm → input flows to output directly ----
    if (!algo_) {
        while (!in_chunks_.empty()) {
            auto chunk = std::move(in_chunks_.front());
            in_chunks_.pop_front();
            if (in_pos_ > 0) {
                chunk = chunk.slice(in_pos_, chunk.size() - in_pos_);
                in_pos_ = 0;
            }
            if (!chunk.empty()) ready_chunks_.push_back(std::move(chunk));
        }
        if (is_last) finished_ = true;
        return;
    }

    while (!in_chunks_.empty() || (is_last && !finished_)) {
        // Ensure writable output chunk
        if (!current_out_ || out_pos_ >= current_out_->size()) {
            publishCurrent();
            if (pool_) {
                current_out_ = pool_->acquire();
                current_out_->resize(pool_->chunkSize());
            } else {
                current_out_ =
                    std::make_shared<std::vector<uint8_t>>(OUT_CHUNK_SIZE);
            }
            out_pos_ = 0;
        }

        auto span_in = readSpan();
        auto span_out = std::span<uint8_t>(current_out_->data() + out_pos_,
                                           current_out_->size() - out_pos_);
        bool final_flag = is_last && in_chunks_.empty();

        auto status = algo_->process(span_in, span_out, final_flag);

        consumeInput(status.bytes_consumed);
        out_pos_ += status.bytes_produced;

        if (status.done) {
            publishCurrent();
            finished_ = true;
            break;
        }
        if (status.need_input) break;
        if (status.need_output) {
            if (out_pos_ >= current_out_->size()) {
                publishCurrent();
                if (pool_) continue;  // stay in loop, get new chunk
                break;  // no pool = single fixed buffer
            }
            break;
        }
    }
}

// ---- drain (free function) ----

auto drain(StreamProcessor& from, StreamProcessor& to) -> void {
    while (true) {
        auto chunk = from.pull();  // ownership transfer
        if (chunk.empty()) break;
        to.push(std::move(chunk));  // zero-copy into next stage
    }
}

}  // namespace compressor::processor

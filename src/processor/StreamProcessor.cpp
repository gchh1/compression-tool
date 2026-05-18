#include "StreamProcessor.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "DebugLog.hpp"
#include "DPFlateBin64kDebug.hpp"

namespace compressor::processor {

// ---- public ----

auto StreamProcessor::push(memory::DataChunk chunk, bool is_last) -> void {
    if (finished_) return;
    if (!chunk.empty()) {
        if (compressor::algorithm::DPFlateBin64kDebug::active()) {
            compressor::processor::dpflate_bin64k_log_pipeline(
                "PIPE_PUSH", "in_chunks.push_back size=%zu is_last=%d", chunk.size(),
                is_last ? 1 : 0);
            auto view = chunk.view();
            compressor::processor::dpflate_bin64k_log_pipeline_bytes(
                "PIPE_CHUNK", "push_head", view.data(), view.size());
        }
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
    if (compressor::algorithm::DPFlateBin64kDebug::active()) {
        compressor::processor::dpflate_bin64k_log_pipeline(
            "PIPE_PUSH_SPAN", "vector copy-construct size=%zu cap=%zu is_last=%d", v->size(),
            v->capacity(), is_last ? 1 : 0);
        compressor::processor::dpflate_bin64k_log_pipeline_bytes(
            "PIPE_CHUNK", "push_head", v->data(), v->size());
    }
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
    compressor::processor::dpflate_bin64k_log_pipeline(
        "PIPE_PUBLISH", "ready_chunks.push_back out_pos=%zu vec_cap=%zu", out_pos_,
        current_out_ ? current_out_->capacity() : 0);
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
        if (!current_out_ || out_pos_ >= current_out_->size()) {
            publishCurrent();
            if (pool_) {
                current_out_ = pool_->acquire();
                current_out_->resize(pool_->chunkSize());
                compressor::processor::dpflate_bin64k_log_pipeline(
                    "PIPE_OUT", "pool.acquire+resize chunkSize=%zu", pool_->chunkSize());
            } else {
                current_out_ =
                    std::make_shared<std::vector<uint8_t>>(OUT_CHUNK_SIZE);
                compressor::processor::dpflate_bin64k_log_pipeline(
                    "PIPE_OUT", "new vector OUT_CHUNK_SIZE=%zu", OUT_CHUNK_SIZE);
            }
            out_pos_ = 0;
        }

        auto span_in = readSpan();
        auto span_out = std::span<uint8_t>(current_out_->data() + out_pos_,
                                           current_out_->size() - out_pos_);
        // ``is_last`` on push means no further plaintext arrives after this chunk.
        // Do not require ``in_chunks_.empty()`` — the current chunk is still queued here.
        bool final_flag = is_last;

        auto status = algo_->process(span_in, span_out, final_flag);

        consumeInput(status.bytes_consumed);
        out_pos_ += status.bytes_produced;

        if (status.done) {
            publishCurrent();
            finished_ = true;
            break;
        }
        if (status.need_input) {
            // When stuck on a tiny front chunk with more chunks queued,
            // merge its trailing bytes into the next chunk so the algorithm
            // sees a continuous span for Huffman tree construction.
            if (status.bytes_consumed == 0 && in_chunks_.size() > 1) {
                size_t remaining = in_chunks_.front().size() - in_pos_;
                if (remaining > 0 && remaining <= 1024) {
                    auto merged = std::make_shared<std::vector<uint8_t>>();
                    auto front_span = readSpan();
                    merged->assign(front_span.begin(), front_span.end());
                    in_chunks_.pop_front();
                    in_pos_ = 0;
                    auto next_span = readSpan();
                    merged->insert(merged->end(), next_span.begin(),
                                   next_span.end());
                    compressor::processor::dpflate_bin64k_log_pipeline(
                        "PIPE_MERGE", "merged.size=%zu cap=%zu front=%zu next=%zu",
                        merged->size(), merged->capacity(), front_span.size(), next_span.size());
                    in_chunks_.pop_front();
                    in_chunks_.push_front(
                        memory::DataChunk::adopt(merged, merged->size()));
                    continue;
                }
            }
            if (is_last && !finished_) continue;
            publishCurrent();
            break;
        }
        if (status.need_output) {
            publishCurrent();
            if (!pool_) continue;
            if (is_last && !finished_) {
                current_out_ = std::make_shared<std::vector<uint8_t>>(
                    pool_->chunkSize());
                out_pos_ = 0;
                continue;
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

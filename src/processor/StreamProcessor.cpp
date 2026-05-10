#include "StreamProcessor.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace compressor::processor {

/**
 * @brief Push chunk
 *
 * @param chunk
 * @param is_last
 */
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

auto StreamProcessor::pull(void) -> memory::DataChunk {
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

/**
 * @brief
 *
 * @return std::vector<uint8_t>
 */
auto StreamProcessor::finish(void) -> std::vector<uint8_t> {
    if (!finished_) {
        processChunks(true);
        publishCurrent();
    }
    return {};
}

/**
 * @brief Seize the front chunk and convert to std::span
 *
 * @return std::span<const uint8_t>
 */
auto StreamProcessor::readSpan(void) const -> std::span<const uint8_t> {
    if (in_chunks_.empty()) return {};
    return in_chunks_.front().view().subspan(in_pos_);
}

/**
 * @brief
 *
 * @param n
 */
auto StreamProcessor::consumeInput(size_t n) -> void {
    in_pos_ += n;
    while (!in_chunks_.empty() && in_pos_ >= in_chunks_.front().size()) {
        in_pos_ -= in_chunks_.front().size();
        in_chunks_.pop_front();
    }
}

/**
 * @brief Once the `current_out_` is full or finish, publish to `ready_chunks_`
 *        and reset the `current_out_` by the deleter defined in the memory
 *        pool.
 *
 */
auto StreamProcessor::publishCurrent() -> void {
    if (!current_out_ || out_pos_ == 0) return;
    ready_chunks_.push_back(memory::DataChunk::adopt(current_out_, out_pos_));
    current_out_.reset();
    out_pos_ = 0;
}

/**
 * @brief Most important process method. For each chunk in the `in_chunks_`, pop
 *        out and process by `algo_`, then push back to `ready_chunks`.
 *
 * @param is_last
 */
auto StreamProcessor::processChunks(bool is_last) -> void {
    // If the `algo_` is nullptr, then output == input
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

        // Obtain span for `algo_->process`
        auto span_in = readSpan();
        auto span_out = std::span<uint8_t>(current_out_->data() + out_pos_,
                                           current_out_->size() - out_pos_);
        bool final_flag = is_last && in_chunks_.empty();

        //* Where the algo_ work
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
            // When needing output, publish current output chunk first and
            publishCurrent();
            if (!pool_) continue;
            if (is_last && !finished_) {
                current_out_ =
                    std::make_shared<std::vector<uint8_t>>(pool_->chunkSize());
                out_pos_ = 0;
                continue;
            }
            break;
        }
    }
}

/**
 * @brief Pull all the ready chunk from `from` and push them to `to` to carry
 * out next step
 *
 * @param from
 * @param to
 */
auto drain(StreamProcessor& from, StreamProcessor& to) -> void {
    while (true) {
        auto chunk = from.pull();  // ownership transfer
        if (chunk.empty()) break;
        to.push(std::move(chunk));  // zero-copy into next stage
    }
}

}  // namespace compressor::processor

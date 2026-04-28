/**
 * @file StreamProcessor.cpp
 * @author yhc
 * @brief
 * @version 0.1
 * @date 2026-04-27
 *
 * @copyright Copyright (c) 2026
 *
 */

#include "StreamProcessor.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace compressor::processor {

/**
 * @brief
 *
 * @param data
 * @param is_last
 */
auto StreamProcessor::push(std::span<const uint8_t> data, bool is_last)
    -> void {
    if (finished_) return;
    in_buffer_.append(data);
    processChunks(is_last);
}

/**
 * @brief
 *
 * @return std::span<const uint8_t>
 */
auto StreamProcessor::pull(void) -> std::span<const uint8_t> {
    return {out_buffer_.data(), out_pos_};
}

/**
 * @brief
 *
 * @param n
 */
auto StreamProcessor::consume(size_t n) -> void {
    if (n > out_pos_) return;
    memmove(out_buffer_.data(), out_buffer_.data() + n, out_pos_ - n);
    out_pos_ -= n;
}

/**
 * @brief
 *
 * @return std::vector<uint8_t>
 */
auto StreamProcessor::finish(void) -> std::vector<uint8_t> {
    if (!finished_) {
        processChunks(true);
    }
    return {out_buffer_.begin(), out_buffer_.begin() + out_pos_};
}

/**
 * @brief
 *
 * @param is_last
 */
auto StreamProcessor::processChunks(bool is_last) -> void {
    while (!in_buffer_.empty() || (is_last && !finished_)) {
        auto span_in = in_buffer_.readSpan();
        auto span_out = std::span<uint8_t>(out_buffer_.data() + out_pos_,
                                           out_buffer_.size() - out_pos_);
        bool final_flag = is_last && in_buffer_.empty();

        auto status = algo_->process(span_in, span_out, final_flag);

        if (status.bytes_consumed > 0)
            in_buffer_.consume(status.bytes_consumed);
        out_pos_ += status.bytes_produced;

        if (status.done) {
            finished_ = true;
            break;
        }
        if (status.need_input && in_buffer_.empty()) break;
        if (status.need_output && out_pos_ == out_buffer_.size()) break;
    }
}

/**
 * @brief Helper function
 *
 * @param from
 * @param to
 */
auto drain(StreamProcessor& from, StreamProcessor& to) -> void {
    while (true) {
        auto out = from.pull();
        if (out.empty()) break;
        to.push(out, false);
        from.consume(out.size());
    }
}

}  // namespace compressor::processor
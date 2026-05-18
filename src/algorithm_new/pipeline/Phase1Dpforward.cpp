#include "pipeline/Phase1Dpforward.hpp"

#include <algorithm>
#include <utility>

#include "BitProcessor.hpp"
#include "ByteView.hpp"
#include "RecordIO.hpp"
#include "Streaming.hpp"

namespace compressor::algorithm::pipeline {

namespace {

void ensure_dp_size(std::vector<models::DPNode>& dp, size_t abs_end) {
    if (dp.size() < abs_end) {
        dp.resize(abs_end, models::DPNode{});
    }
}

void flush_dp_range(
    streaming::File_Chunk_Writer& writer,
    const std::vector<models::DPNode>& dp,
    size_t abs_begin,
    size_t abs_end,
    compressor::utils::_buffer& pending) {
    if (abs_begin >= abs_end || abs_end > dp.size()) {
        return;
    }
    std::vector<models::DPNode> slice(
        dp.begin() + static_cast<std::ptrdiff_t>(abs_begin),
        dp.begin() + static_cast<std::ptrdiff_t>(abs_end));
    writer.write_chunk(record_io::dp_nodes_to_u8(slice, pending));
}

size_t process_end_exclusive(
    size_t vb_base,
    const streaming::VirtualBuffer<std::vector<uint8_t>>& data_vb) {
    const size_t window_end = vb_base + data_vb.size();
    if (data_vb.num_chunks() >= 2) {
        return window_end - data_vb.new_chunk_size();
    }
    return window_end;
}

void try_release_front_u8(
    size_t& vb_base,
    streaming::VirtualBuffer<std::vector<uint8_t>>& data_vb,
    size_t processed_abs,
    size_t search_size) {
    while (!data_vb.empty()) {
        const size_t front = data_vb.prev_chunk_size();
        if (vb_base + front > processed_abs || processed_abs < search_size) {
            break;
        }
        if (vb_base + front > processed_abs - search_size) {
            break;
        }
        vb_base += front;
        data_vb.pop();
    }
}

}  // namespace

Phase1Result run_phase1_dpforward(
    LZDP& lzdp,
    const LZDPConfig& config,
    const std::string& input_path,
    const std::string& temp_a_path,
    size_t chunk_size) {
    Phase1Result result;
    streaming::File_Chunk_Reader reader(input_path, chunk_size);
    streaming::File_Chunk_Writer writer(temp_a_path);

    std::vector<uint8_t> cur = reader.read_chunk();
    std::vector<uint8_t> next = reader.read_chunk();
    streaming::VirtualBuffer<std::vector<uint8_t>> data_vb(std::move(cur), std::move(next));

    if (data_vb.empty()) {
        return result;
    }

    std::vector<models::DPNode> dp_store;
    ensure_dp_size(dp_store, data_vb.size());
    dp_store[0] = models::DPNode(0, 0, -1, Triple(0, 1, data_vb[0]));

    size_t next_abs = 0;
    size_t flushed_dp_end = 0;
    size_t vb_base = 0;
    compressor::utils::_buffer write_pending;

    VbByteInput input_view{data_vb, vb_base};

    auto run_forward = [&](size_t abs_begin, size_t abs_end) {
        ensure_dp_size(dp_store, abs_end);
        input_view.base = vb_base;
        lzdp.dpforward(input_view, dp_store, abs_begin, abs_end);
    };

    size_t proc_end = process_end_exclusive(vb_base, data_vb);
    run_forward(next_abs, proc_end);
    flush_dp_range(writer, dp_store, flushed_dp_end, proc_end, write_pending);
    flushed_dp_end = proc_end;
    next_abs = proc_end;
    try_release_front_u8(vb_base, data_vb, proc_end, config.window.search_size);

    while (!reader.is_end()) {
        std::vector<uint8_t> fresh = reader.read_chunk();
        data_vb.append(std::move(fresh));
        ensure_dp_size(dp_store, vb_base + data_vb.size());

        proc_end = process_end_exclusive(vb_base, data_vb);
        run_forward(next_abs, proc_end);
        flush_dp_range(writer, dp_store, flushed_dp_end, proc_end, write_pending);
        flushed_dp_end = proc_end;
        next_abs = proc_end;
        try_release_front_u8(vb_base, data_vb, proc_end, config.window.search_size);
    }

    proc_end = vb_base + data_vb.size();
    run_forward(next_abs, proc_end);
    flush_dp_range(writer, dp_store, flushed_dp_end, proc_end, write_pending);

    result.total_input_bytes = proc_end;
    if (proc_end > 0 && dp_store.size() >= proc_end) {
        result.terminal_node = dp_store[proc_end - 1];
    }
    return result;
}

}  // namespace compressor::algorithm::pipeline



#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>

#include "LZDP.hpp"

#include "ByteView.hpp"
#include "Streaming.hpp"
#include "StreamingCancel.hpp"




namespace compressor::algorithm {

template <typename Input, typename DPNodes>
void LZDP::dpforward(
    const Input& input,
    DPNodes& dp,
    size_t begin,
    size_t end,
    size_t match_end)
{
    if (end == 0) {
        end = begin + input.window_size();
    }
    if (match_end == 0) {
        match_end = end;
    }

    for (size_t pos = begin; pos < end; ++pos) {
        if (core_new::is_streaming_cancel_requested()) {
            throw std::runtime_error("cancelled");
        }
        if (dp[pos].pre_pos == -2) continue;

        const size_t lit_lit = dp[pos].literal_count + 1;
        const size_t lit_mat = dp[pos].match_count;
        const size_t lit_cost = cal_cost(lit_lit, lit_mat);

        if (pos + 1 <= end && pos + 1 < dp.size()) {
            if (dp[pos + 1].pre_pos == -2) {
                dp[pos + 1] = models::DPNode(lit_lit, lit_mat, static_cast<int>(pos), Triple(0, 0, input[pos]));
            } else {
                size_t next_cost = cal_cost(dp[pos + 1].literal_count, dp[pos + 1].match_count);
                if (lit_cost < next_cost) {
                    dp[pos + 1].literal_count = lit_lit;
                    dp[pos + 1].match_count = lit_mat;
                    dp[pos + 1].pre_pos = static_cast<int>(pos);
                    dp[pos + 1].triple = Triple(0, 0, input[pos]);
                }
            }
        }

        const size_t search_len = (pos > config_.window.search_size) ? config_.window.search_size : pos;
        const size_t search_start = pos - search_len;
        const size_t remain = match_end - pos;
        const size_t look_len = (remain > config_.window.look_size) ? config_.window.look_size : remain;

        RelativeByteSlice<Input> search_view{input, search_start};
        RelativeByteSlice<Input> look_view{input, pos};

        std::vector<Triple> match_results;
        if (config_.dp.match_engine == models::MatchEngine::KMP) {
            match_results = LZMatcher::kmpSearch(
                search_view, search_len, look_view, look_len,
                config_.dp.dp_top, config_.window.min_match_len);
        } else if (config_.dp.match_engine == models::MatchEngine::HashChain) {
            match_results = LZMatcher::hashChainSearch(
                search_view, search_len, look_view, look_len,
                config_.dp.dp_top, config_.window.min_match_len);
        }

        for (const Triple& kr : match_results) {
            if (kr.offset == 0) continue;
            const size_t target = pos + kr.length;
            if (target > match_end || target >= dp.size()) continue;

            const size_t mat_lit = dp[pos].literal_count;
            const size_t mat_mat = dp[pos].match_count + 1;
            const size_t mat_cost = cal_cost(mat_lit, mat_mat);

            if (dp[target].pre_pos == -2) {
                dp[target] = models::DPNode(mat_lit, mat_mat, static_cast<int>(pos), Triple(kr.offset, kr.length, 0));
            } else {
                size_t tgt_cost = cal_cost(dp[target].literal_count, dp[target].match_count);
                if (mat_cost < tgt_cost) {
                    dp[target].literal_count = mat_lit;
                    dp[target].match_count = mat_mat;
                    dp[target].pre_pos = static_cast<int>(pos);
                    dp[target].triple = Triple(kr.offset, kr.length, 0);
                }
            }
        }
    }
}

template <typename DpNodes>
std::vector<Triple> LZDP::dpbacktrack(
    DpNodes& dp,
    int& cur_pos,
    size_t begin)
{   
    std::vector<Triple> triples;
    if (cur_pos == 0){
        cur_pos = static_cast<int>(dp.size() - 1);
    }

    models::DPNode cur = dp[cur_pos];

    while(cur.pre_pos != -2){
        if (core_new::is_streaming_cancel_requested()) {
            throw std::runtime_error("cancelled");
        }
        triples.push_back(cur.triple);
        if (cur.pre_pos < 0) break;
        cur = dp[cur.pre_pos];
        cur_pos = cur.pre_pos;
    }
    std::reverse(triples.begin(), triples.end());
    if (!triples.empty() && triples[0].offset == 0 && triples[0].length == 0 && triples[0].literal == 0) {
        triples.erase(triples.begin());
    }
    return triples;
}

// std::vector<Triple> LZDP::compress_nonstreaming(const std::vector<uint8_t>& input)
// {   
//     size_t inlen = input.size();
//     std::vector<models::DPNode*> dp(inlen, nullptr);
//     dp[0] = new models::DPNode(0,0,-1,Triple(0,1,input[0]));
//     dpforward<std::vector<uint8_t>, std::vector<models::DPNode*>>(input, dp);
//     int cur_pos = 0;//只是为了填充dpbacktrack的参数，实际值无意义
//     return dpbacktrack<std::vector<models::DPNode*>>(dp, cur_pos);
// }











template void LZDP::dpforward<VectorByteInput, std::vector<models::DPNode>>(
    const VectorByteInput&, std::vector<models::DPNode>&, size_t, size_t, size_t);

template void LZDP::dpforward<VbByteInput, std::vector<models::DPNode>>(
    const VbByteInput&, std::vector<models::DPNode>&, size_t, size_t, size_t);

template std::vector<Triple> LZDP::dpbacktrack<std::vector<models::DPNode>>(
    std::vector<models::DPNode>&, int&, size_t);

namespace {

/// ``dpforward`` / ``dpbacktrack`` view with absolute indices ``[dp_base, dp_base+store.size())``.
struct RelativeDpStore {
    std::vector<models::DPNode>& store;
    size_t base{0};

    size_t size() const { return base + store.size(); }

    models::DPNode& operator[](size_t abs) { return store.at(abs - base); }

    const models::DPNode& operator[](size_t abs) const { return store.at(abs - base); }
};

}  // namespace

template void LZDP::dpforward<VbByteInput, RelativeDpStore>(
    const VbByteInput&, RelativeDpStore&, size_t, size_t, size_t);

}  // namespace compressor::algorithm

// ──── Pipeline ────

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>

#include "BitProcessor.hpp"
#include "ByteView.hpp"
#include "EncodingTriple.hpp"
#include "RecordIO.hpp"
#include "Streaming.hpp"

namespace compressor::algorithm::pipeline {

namespace fs = std::filesystem;

// ──── Non-streaming ────

namespace {

void init_dp_frontier(std::vector<models::DPNode>& dp, const std::vector<uint8_t>& input) {
    if (input.empty()) {
        return;
    }
    dp.assign(input.size() + 1, models::DPNode{0, 0, -2, Triple(0, 0, 0)});
    dp[0] = models::DPNode(0, 0, -1, Triple(0, 0, 0));
}

}  // namespace

LZDPNonStreamingResult compress_bytes(
    const std::vector<uint8_t>& input,
    const LZDPConfig& config) {
    LZDPNonStreamingResult result;
    if (input.empty()) {
        return result;
    }

    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }

    LZDP lzdp(config);
    std::vector<models::DPNode> dp;
    init_dp_frontier(dp, input);

    VectorByteInput view{input, 0};
    lzdp.dpforward(view, dp, 0, input.size());

    int cur_pos = 0;
    std::vector<Triple> triples = lzdp.dpbacktrack(dp, cur_pos, 0);

    if (!config.encoding.use_flag_encoding) {
        triples = literalrun(triples, config.window.look_size);
    }

    compressor::utils::_buffer pending;
    result.triples = std::move(triples);
    result.compressed =
        encoding_triple_lz(result.triples, config.encoding, pending, true);
    return result;
}

std::vector<uint8_t> decompress_bytes(
    const std::vector<uint8_t>& compressed,
    const LZDPConfig& config) {
    compressor::utils::_buffer pending;
    std::vector<Triple> triples = readtriple(compressed, config.encoding, pending);
    return decode_triple(triples, config.encoding, pending);
}

// ──── Phase 1 ────

namespace {

void ensure_dp_abs(std::vector<models::DPNode>& dp, size_t dp_base, size_t abs_end) {
    if (abs_end < dp_base) {
        return;
    }
    const size_t need = abs_end - dp_base + 1;
    if (dp.size() < need) {
        dp.resize(need, models::DPNode{});
    }
}

/// After flush, keep only the frontier node at ``keep_abs`` (§1.18: release flushed DP slots).
void shrink_dp_after_flush(std::vector<models::DPNode>& dp,
                           size_t& dp_base,
                           size_t keep_abs) {
    if (keep_abs < dp_base) {
        throw std::runtime_error("LZDP phase1: shrink keep_abs < dp_base");
    }
    const size_t rel = keep_abs - dp_base;
    if (rel >= dp.size()) {
        throw std::runtime_error("LZDP phase1: shrink keep_abs out of range");
    }
    models::DPNode frontier = dp[rel];
    dp_base = keep_abs;
    dp.clear();
    dp.push_back(std::move(frontier));
}

void flush_dp_range(
    streaming::File_Chunk_Writer& writer,
    const std::vector<models::DPNode>& dp,
    size_t dp_base,
    size_t abs_begin,
    size_t abs_end,
    compressor::utils::_buffer& pending) {
    if (abs_begin >= abs_end || abs_end <= dp_base) {
        return;
    }
    const size_t rel_begin = abs_begin - dp_base;
    const size_t rel_end = abs_end - dp_base;
    if (rel_end > dp.size()) {
        throw std::runtime_error("LZDP phase1: flush range exceeds dp_store");
    }
    std::vector<models::DPNode> slice(
        dp.begin() + static_cast<std::ptrdiff_t>(rel_begin),
        dp.begin() + static_cast<std::ptrdiff_t>(rel_end));
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

    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }

    std::vector<uint8_t> cur = reader.read_chunk();
    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }

    std::vector<uint8_t> next = reader.read_chunk();
    streaming::VirtualBuffer<std::vector<uint8_t>> data_vb(std::move(cur), std::move(next));

    if (data_vb.empty()) {
        return result;
    }

    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }

    std::vector<models::DPNode> dp_store;
    size_t dp_base = 0;
    dp_store.push_back(models::DPNode(0, 0, -1, Triple(0, 0, 0)));

    size_t next_abs = 0;
    size_t flushed_dp_end = 0;
    size_t vb_base = 0;
    compressor::utils::_buffer write_pending;

    VbByteInput input_view{data_vb, vb_base};
    RelativeDpStore dp_view{dp_store, dp_base};

    auto run_forward = [&](size_t abs_begin, size_t abs_end) {
        const size_t avail_end = vb_base + data_vb.size();
        const size_t ext_end = std::min(abs_end + config.window.look_size, avail_end);
        ensure_dp_abs(dp_store, dp_base, ext_end);
        dp_view.base = dp_base;
        input_view.base = vb_base;
        lzdp.dpforward(input_view, dp_view, abs_begin, abs_end, ext_end);
    };

    size_t proc_end = process_end_exclusive(vb_base, data_vb);
    run_forward(next_abs, proc_end);
    flush_dp_range(writer, dp_store, dp_base, flushed_dp_end, proc_end, write_pending);
    flushed_dp_end = proc_end;
    next_abs = proc_end;
    shrink_dp_after_flush(dp_store, dp_base, proc_end);
    try_release_front_u8(vb_base, data_vb, proc_end, config.window.search_size);

    while (!reader.is_end()) {
        if (core_new::is_streaming_cancel_requested()) {
            throw std::runtime_error("cancelled");
        }
        std::vector<uint8_t> fresh = reader.read_chunk();
        data_vb.append(std::move(fresh));
        ensure_dp_abs(dp_store, dp_base, vb_base + data_vb.size());

        proc_end = process_end_exclusive(vb_base, data_vb);
        run_forward(next_abs, proc_end);
        flush_dp_range(writer, dp_store, dp_base, flushed_dp_end, proc_end, write_pending);
        flushed_dp_end = proc_end;
        next_abs = proc_end;
        shrink_dp_after_flush(dp_store, dp_base, proc_end);
        try_release_front_u8(vb_base, data_vb, proc_end, config.window.search_size);
    }

    proc_end = vb_base + data_vb.size();
    run_forward(next_abs, proc_end);
    flush_dp_range(writer, dp_store, dp_base, flushed_dp_end, proc_end, write_pending);

    result.total_input_bytes = proc_end;
    if (proc_end >= dp_base && proc_end - dp_base < dp_store.size()) {
        result.terminal_node = dp_store[proc_end - dp_base];
    }
    return result;
}

// ──── Phase 2 ────

namespace {

void write_temp_b_reverse(
    const std::vector<Triple>& triples,
    const std::string& temp_b_path) {
    const size_t nbytes = triples.size() * record_io::kTripleRecordBytes;
    streaming::Reverse_File_Chunk_Writer writer(temp_b_path);
    writer.preallocate(nbytes);

    compressor::utils::_buffer pending;
    std::vector<uint8_t> batch;
    batch.reserve(record_io::kTripleRecordBytes * 256);

    for (auto it = triples.rbegin(); it != triples.rend(); ++it) {
        auto rec = record_io::triple_to_record_bytes(*it, pending);
        if (rec.size() != record_io::kTripleRecordBytes) {
            throw std::runtime_error("Phase2: triple record size mismatch");
        }
        batch.insert(batch.end(), rec.begin(), rec.end());
        if (batch.size() >= record_io::kTripleRecordBytes * 256) {
            writer.write_chunk_reverse(batch);
            batch.clear();
        }
    }

    if (!batch.empty()) {
        writer.write_chunk_reverse(batch);
    }
}

/// §1.19 TempA: fixed-width DP records; indexed read avoids O(n) ``dp_store`` in memory.
class DpNodeIndexedStore {
public:
    void open(const std::string& temp_a_path, size_t n, models::DPNode terminal) {
        file_.open(temp_a_path, std::ios::binary);
        if (!file_) {
            throw std::runtime_error("DpNodeIndexedStore: cannot open " + temp_a_path);
        }
        n_ = n;
        terminal_ = std::move(terminal);
    }

    const models::DPNode& at(size_t abs_pos) {
        if (abs_pos == n_) {
            return terminal_;
        }
        if (abs_pos >= n_) {
            throw std::runtime_error("DpNodeIndexedStore: abs_pos out of range");
        }
        const auto off = static_cast<std::streamoff>(
            abs_pos * static_cast<std::streamoff>(record_io::kDPNodeRecordBytes));
        file_.clear();
        file_.seekg(off, std::ios::beg);
        std::vector<uint8_t> rec(record_io::kDPNodeRecordBytes);
        file_.read(reinterpret_cast<char*>(rec.data()),
                   static_cast<std::streamsize>(rec.size()));
        if (!file_ || file_.gcount() != static_cast<std::streamsize>(rec.size())) {
            throw std::runtime_error("DpNodeIndexedStore: short read at index " +
                                   std::to_string(abs_pos));
        }
        bit_pending_ = {};
        auto nodes = record_io::parse_dp_nodes_bytes(rec, bit_pending_);
        if (nodes.size() != 1) {
            throw std::runtime_error("DpNodeIndexedStore: expected one DP record");
        }
        scratch_ = std::move(nodes[0]);
        return scratch_;
    }

private:
    std::ifstream file_;
    size_t n_{0};
    models::DPNode terminal_{};
    models::DPNode scratch_{};
    compressor::utils::_buffer bit_pending_{};
};

std::vector<Triple> backtrack_with_indexed_store(DpNodeIndexedStore& store, size_t n) {
    std::vector<Triple> triples;
    if (n == 0) {
        return triples;
    }

    int cur_pos = static_cast<int>(n);
    models::DPNode cur = store.at(static_cast<size_t>(cur_pos));

    while (cur.pre_pos != -2) {
        if (core_new::is_streaming_cancel_requested()) {
            throw std::runtime_error("cancelled");
        }
        triples.push_back(cur.triple);
        if (cur.pre_pos < 0) {
            break;
        }
        cur_pos = cur.pre_pos;
        cur = store.at(static_cast<size_t>(cur_pos));
    }

    std::reverse(triples.begin(), triples.end());
    if (!triples.empty() && triples[0].offset == 0 && triples[0].length == 0 &&
        triples[0].literal == 0) {
        triples.erase(triples.begin());
    }
    return triples;
}

}  // namespace

Phase2Result run_phase2_dpbacktrack(
    LZDP&,
    const Phase1Result& phase1,
    const std::string& temp_a_path,
    const std::string& temp_b_path,
    size_t chunk_size) {
    Phase2Result result;

    const size_t n = phase1.total_input_bytes;
    result.total_tokens =
        phase1.terminal_node.literal_count + phase1.terminal_node.match_count;

    if (n == 0) {
        streaming::Reverse_File_Chunk_Writer writer(temp_b_path);
        writer.preallocate(0);
        return result;
    }

    (void)chunk_size;
    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }
    DpNodeIndexedStore dp_index;
    dp_index.open(temp_a_path, n, phase1.terminal_node);
    result.triples = backtrack_with_indexed_store(dp_index, n);

    if (result.triples.size() != result.total_tokens) {
        throw std::runtime_error("Phase2: token count mismatch");
    }

    write_temp_b_reverse(result.triples, temp_b_path);
    return result;
}

// ──── Streaming ────

LZDPStreamingPipeline::LZDPStreamingPipeline(LZDPConfig config, LZDPStreamingOptions options)
    : config_(std::move(config)), options_(std::move(options)), lzdp_(config_) {}

std::string LZDPStreamingPipeline::temp_a_path() const {
    return (fs::path(options_.workspace_dir) / options_.temp_a_name).string();
}

std::string LZDPStreamingPipeline::temp_b_path() const {
    return (fs::path(options_.workspace_dir) / options_.temp_b_name).string();
}

void LZDPStreamingPipeline::compress_file(const std::string& input_path,
                                          const std::string& output_path) {
    fs::create_directories(options_.workspace_dir);

    std::error_code ec;
    fs::remove(temp_a_path(), ec);
    fs::remove(temp_b_path(), ec);

    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }

    const Phase1Result phase1 = run_phase1_dpforward(
        lzdp_,
        config_,
        input_path,
        temp_a_path(),
        options_.chunk_size);

    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }

    const Phase2Result phase2 = run_phase2_dpbacktrack(
        lzdp_,
        phase1,
        temp_a_path(),
        temp_b_path(),
        options_.chunk_size);

    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }

    compressor::utils::_buffer emit_pending;
    auto tr = phase2.triples;
    if (!config_.encoding.use_flag_encoding) {
        tr = literalrun(tr, config_.window.look_size);
    }
    if (core_new::is_streaming_cancel_requested()) {
        throw std::runtime_error("cancelled");
    }
    std::vector<uint8_t> compressed = encoding_triple_lz(tr, config_.encoding, emit_pending, true);

    {
        std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("LZDPStreamingPipeline: cannot open " + output_path);
        }
        if (!compressed.empty()) {
            out.write(reinterpret_cast<const char*>(compressed.data()),
                      static_cast<std::streamsize>(compressed.size()));
        }
    }
}

}  // namespace compressor::algorithm::pipeline

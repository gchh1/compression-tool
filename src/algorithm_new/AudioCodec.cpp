#include "AudioCodec.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>

namespace compressor::algorithm {

namespace {

// ── CRC tables ──

std::array<uint8_t, 256> make_crc8_table() {
    std::array<uint8_t, 256> table{};
    for (int i = 0; i < 256; ++i) {
        uint8_t crc = static_cast<uint8_t>(i);
        for (int j = 0; j < 8; ++j)
            crc = static_cast<uint8_t>((crc << 1) ^ ((crc & 0x80) ? 0x07 : 0));
        table[i] = crc;
    }
    return table;
}

std::array<uint16_t, 256> make_crc16_table() {
    std::array<uint16_t, 256> table{};
    for (int i = 0; i < 256; ++i) {
        uint16_t crc = static_cast<uint16_t>(i << 8);
        for (int j = 0; j < 8; ++j)
            crc = static_cast<uint16_t>((crc << 1) ^ ((crc & 0x8000) ? 0x8005 : 0));
        table[i] = crc;
    }
    return table;
}

const auto CRC8_TABLE = make_crc8_table();
const auto CRC16_TABLE = make_crc16_table();

uint8_t crc8(const uint8_t* data, size_t len, uint8_t init = 0) {
    uint8_t crc = init;
    for (size_t i = 0; i < len; ++i)
        crc = CRC8_TABLE[crc ^ data[i]];
    return crc;
}

uint16_t crc16(const uint8_t* data, size_t len, uint16_t init = 0) {
    uint16_t crc = init;
    for (size_t i = 0; i < len; ++i)
        crc = static_cast<uint16_t>((crc << 8) ^ CRC16_TABLE[((crc >> 8) ^ data[i]) & 0xFF]);
    return crc;
}

// ── FLAC bit writer (MSB-first) ──

class FlacBitWriter {
    std::vector<uint8_t> buf_;
    int bit_pos_ = 0;

public:
    void write_bit(int bit) {
        if (bit_pos_ == 0) buf_.push_back(0);
        if (bit) buf_.back() |= static_cast<uint8_t>(1 << (7 - bit_pos_));
        bit_pos_ = (bit_pos_ + 1) & 7;
    }

    void write_bits(uint32_t val, int nbits) {
        for (int i = nbits - 1; i >= 0; --i)
            write_bit((val >> i) & 1);
    }

    void write_utf8(uint64_t val) {
        if (val < 0x80) {
            write_bits(static_cast<uint32_t>(val), 8);
        } else if (val < 0x4000) {
            write_bits(0xC0 | static_cast<uint32_t>((val >> 6) & 0x1F), 8);
            write_bits(0x80 | static_cast<uint32_t>(val & 0x3F), 8);
        } else if (val < 0x200000) {
            write_bits(0xE0 | static_cast<uint32_t>((val >> 12) & 0x0F), 8);
            write_bits(0x80 | static_cast<uint32_t>((val >> 6) & 0x3F), 8);
            write_bits(0x80 | static_cast<uint32_t>(val & 0x3F), 8);
        } else if (val < 0x10000000) {
            write_bits(0xF0 | static_cast<uint32_t>((val >> 18) & 0x07), 8);
            write_bits(0x80 | static_cast<uint32_t>((val >> 12) & 0x3F), 8);
            write_bits(0x80 | static_cast<uint32_t>((val >> 6) & 0x3F), 8);
            write_bits(0x80 | static_cast<uint32_t>(val & 0x3F), 8);
        } else {
            write_bits(0xF8 | static_cast<uint32_t>((val >> 24) & 0x03), 8);
            write_bits(0x80 | static_cast<uint32_t>((val >> 18) & 0x3F), 8);
            write_bits(0x80 | static_cast<uint32_t>((val >> 12) & 0x3F), 8);
            write_bits(0x80 | static_cast<uint32_t>((val >> 6) & 0x3F), 8);
            write_bits(0x80 | static_cast<uint32_t>(val & 0x3F), 8);
        }
    }

    void flush() { bit_pos_ = 0; }
    int get_bit_pos() const { return bit_pos_; }

    void write_byte(uint8_t b) {
        if (bit_pos_ == 0) buf_.push_back(b);
        else write_bits(b, 8);
    }

    void write_bytes(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) write_byte(data[i]);
    }

    const std::vector<uint8_t>& buffer() const { return buf_; }
    std::vector<uint8_t> take_buffer() { flush(); return std::move(buf_); }
    size_t size() const { return buf_.size(); }

    void replace_byte(size_t pos, uint8_t b) {
        if (pos < buf_.size()) buf_[pos] = b;
    }
};

// ── LPC: Autocorrelation ──

void compute_autocorr(const int32_t* data, int n, double* ac, int max_order) {
    for (int lag = 0; lag <= max_order; ++lag) {
        double sum = 0.0;
        for (int i = 0; i < n - lag; ++i)
            sum += static_cast<double>(data[i]) * static_cast<double>(data[i + lag]);
        ac[lag] = sum;
    }
}

// ── Levinson-Durbin recursion ──
// Solves Yule-Walker equations to compute LPC coefficients.
// Input: autocorrelation ac[0..max_order]
// Output: lpc_coeffs[0..max_order-1] (prediction coefficients a_1..a_p)
//         refl_coeffs[0..max_order-1] (reflection/PARCOR coefficients)
// Returns: prediction error power

double levinson_durbin(const double* ac, int max_order,
                       double* lpc_coeffs, double* refl_coeffs) {
    if (ac[0] <= 0.0) {
        for (int i = 0; i < max_order; ++i) {
            lpc_coeffs[i] = 0.0;
            refl_coeffs[i] = 0.0;
        }
        return 1.0;
    }

    std::vector<double> prev_lpc(max_order, 0.0);
    double prev_err = ac[0];

    for (int m = 0; m < max_order; ++m) {
        double num = ac[m + 1];
        for (int i = 0; i < m; ++i)
            num -= prev_lpc[i] * ac[m - i];

        double refl = num / prev_err;

        refl_coeffs[m] = refl;
        lpc_coeffs[m] = refl;

        for (int i = 0; i < m; ++i)
            lpc_coeffs[i] = prev_lpc[i] - refl * prev_lpc[m - 1 - i];

        prev_err *= (1.0 - refl * refl);
        if (prev_err <= 0.0) prev_err = 1e-15;

        for (int i = 0; i <= m; ++i)
            prev_lpc[i] = lpc_coeffs[i];
    }

    return prev_err;
}

// ── Quantize LPC coefficients to FLAC fixed-point ──
// LPC precision in FLAC: 15 bits for coefficients, plus shift
// coeff[i] = qlp_coeff[i] / 2^shift, shift = precision - 1

struct LpcQuantResult {
    int shift;
    int32_t qlp_coeff[32];
};

LpcQuantResult quantize_lpc(const double* lpc_coeffs, int order) {
    LpcQuantResult result{};
    result.shift = 0;

    constexpr int max_precision = 15;
    constexpr double max_val = static_cast<double>((1 << (max_precision - 1)) - 1);

    double max_abs = 0.0;
    for (int i = 0; i < order; ++i) {
        double abs_val = std::abs(lpc_coeffs[i]);
        if (abs_val > max_abs) max_abs = abs_val;
    }

    if (max_abs <= 1e-12) return result;

    int total_shift = max_precision - 1;
    int shift = 0;
    while (max_abs * (1.0 * (1 << shift)) <= max_val / 2.0 && shift < 15)
        ++shift;
    result.shift = total_shift - shift;

    double scale = static_cast<double>(1LL << result.shift);
    for (int i = 0; i < order; ++i) {
        double qval = std::round(lpc_coeffs[i] * scale);
        result.qlp_coeff[i] = static_cast<int32_t>(std::max(-32767.0, std::min(32767.0, qval)));
    }

    return result;
}

// ── LPC compute residual ──

void lpc_residual(const int32_t* data, int n, const LpcQuantResult& qlpc,
                  int order, int32_t* residual) {
    for (int i = 0; i < order && i < n; ++i)
        residual[i] = data[i];

    for (int i = order; i < n; ++i) {
        int64_t pred = 0;
        for (int j = 0; j < order; ++j)
            pred += static_cast<int64_t>(qlpc.qlp_coeff[j]) * static_cast<int64_t>(data[i - 1 - j]);
        pred >>= qlpc.shift;
        residual[i] = static_cast<int32_t>(static_cast<int64_t>(data[i]) - pred);
    }
}

// ── Fixed predictor (orders 0-4) ──

int64_t fixed_predict(int order, const int32_t* data, int pos) {
    switch (order) {
        case 0: return 0;
        case 1: return data[pos - 1];
        case 2: return 2LL * data[pos - 1] - data[pos - 2];
        case 3: return 3LL * data[pos - 1] - 3LL * data[pos - 2] + data[pos - 3];
        case 4: return 4LL * data[pos - 1] - 6LL * data[pos - 2] +
                             4LL * data[pos - 3] - data[pos - 4];
        default: return 0;
    }
}

// ── Compute residual for a predictor and estimate Rice coding cost ──

uint64_t estimate_residual_energy(int block_size,
                                   int32_t* residual) {
    uint64_t sum_abs = 0;
    for (int i = 0; i < block_size; ++i) {
        int32_t r = residual[i];
        sum_abs += static_cast<uint64_t>(r < 0 ? static_cast<uint64_t>(-r) : static_cast<uint64_t>(r));
    }
    return sum_abs;
}

// ── Fold signed to unsigned for Rice coding ──

uint32_t fold_signed(int32_t val) {
    if (val >= 0) return static_cast<uint32_t>(val) << 1;
    return static_cast<uint32_t>((static_cast<uint64_t>(-val) << 1) - 1);
}

// ── Estimate bits needed for Rice coding residuals ──

uint64_t estimate_rice_bits(const int32_t* residuals, int count, int k) {
    uint64_t total_bits = 0;
    uint32_t mask = (static_cast<uint32_t>(1) << k) - 1;
    for (int i = 0; i < count; ++i) {
        uint32_t u = fold_signed(residuals[i]);
        uint32_t q = u >> k;
        total_bits += q + 1 + (k > 0 ? static_cast<uint64_t>(k) : 0);
    }
    return total_bits;
}

// ── Optimal Rice parameter search ──

int estimate_rice_param(const int32_t* residuals, int count) {
    if (count == 0) return 0;
    uint64_t sum_abs = 0;
    for (int i = 0; i < count; ++i)
        sum_abs += residuals[i] < 0 ? static_cast<uint64_t>(-residuals[i])
                                     : static_cast<uint64_t>(residuals[i]);
    uint64_t mean = sum_abs / static_cast<uint64_t>(count);
    if (mean == 0) return 0;
    int k = 0;
    while ((static_cast<uint64_t>(1) << (k + 1)) <= mean && k < 14) ++k;
    return std::min(k, 14);
}

// ── Partition order optimization ──
// Returns best partition order (0..8) and total estimated bits

struct PartitionResult {
    int best_order;
    uint64_t best_bits;
    int best_params[256];  // Rice k per partition
};

PartitionResult find_best_partition(const int32_t* residuals, int count,
                                     int base_k) {
    PartitionResult best{};
    best.best_order = 0;
    best.best_bits = std::numeric_limits<uint64_t>::max();

    int max_po = 0;
    int n = count;
    while ((1 << max_po) <= n / 4 && max_po < 8) ++max_po;

    for (int po = 0; po <= max_po; ++po) {
        int num_partitions = 1 << po;
        int base_count = count / num_partitions;
        int extra = count % num_partitions;

        uint64_t total_bits = static_cast<uint64_t>(4) * static_cast<uint64_t>(num_partitions);
        // Actually partition order overhead: 4 bits per partition for Rice parameter
        // Plus 4 bits for partition order itself

        int offset = 0;
        for (int p = 0; p < num_partitions; ++p) {
            int pcount = base_count + (p < extra ? 1 : 0);
            if (pcount == 0) continue;
            int k = estimate_rice_param(residuals + offset, pcount);
            total_bits += estimate_rice_bits(residuals + offset, pcount, k);
            offset += pcount;
        }

        if (total_bits < best.best_bits) {
            best.best_order = po;
            best.best_bits = total_bits;

            offset = 0;
            for (int p = 0; p < num_partitions; ++p) {
                int pcount = base_count + (p < extra ? 1 : 0);
                if (pcount == 0) {
                    best.best_params[p] = 0;
                    continue;
                }
                best.best_params[p] = estimate_rice_param(residuals + offset, pcount);
                offset += pcount;
            }
        }
    }

    return best;
}

// ── Write Rice residuals ──

void write_rice_residuals(FlacBitWriter& w, const int32_t* residuals,
                           int count, int k) {
    uint32_t mask = (static_cast<uint32_t>(1) << k) - 1;
    for (int i = 0; i < count; ++i) {
        uint32_t u = fold_signed(residuals[i]);
        uint32_t q = u >> k;
        uint32_t r = u & mask;
        for (uint32_t j = 0; j < q; ++j) w.write_bit(1);
        w.write_bit(0);
        if (k > 0) w.write_bits(r, k);
    }
}

void write_partitioned_rice(FlacBitWriter& w, const int32_t* residuals,
                              int count, int po, const int* k_params) {
    w.write_bits(static_cast<uint32_t>(po), 4);

    int num_partitions = 1 << po;
    int base_count = count / num_partitions;
    int extra = count % num_partitions;

    int offset = 0;
    for (int p = 0; p < num_partitions; ++p) {
        int pcount = base_count + (p < extra ? 1 : 0);
        int k = k_params[p];
        w.write_bits(static_cast<uint32_t>(k), 4);
        if (pcount > 0)
            write_rice_residuals(w, residuals + offset, pcount, k);
        offset += pcount;
    }
}

// ── Mid/side stereo decorrelation ──

enum class StereoMethod { Independent, MidSide };

struct StereoEstimate {
    StereoMethod method;
    uint64_t total_bits;
};

StereoEstimate evaluate_stereo(const std::vector<int32_t>& left_block,
                                const std::vector<int32_t>& right_block,
                                int block_size, int order,
                                const int32_t* left_lpc_coeff, int lpc_shift,
                                int max_lpc_order) {
    StereoEstimate result{};
    result.method = StereoMethod::Independent;
    result.total_bits = 0;

    std::vector<int32_t> left_res(block_size);
    std::vector<int32_t> right_res(block_size);

    // Independent estimation
    lpc_residual(left_block.data(), block_size, {lpc_shift, {}},
                 order, left_res.data());
    lpc_residual(right_block.data(), block_size, {lpc_shift, {}},
                 order, right_res.data());

    // Warning: simplified - uses order 0 LPC for right channel bits estimation
    // Full implementation would compute separate LPC for right channel
    int left_k = estimate_rice_param(left_res.data() + order, block_size - order);
    int right_k = estimate_rice_param(right_res.data() + order, block_size - order);

    uint64_t independent_bits = estimate_rice_bits(left_res.data() + order, block_size - order, left_k)
                               + estimate_rice_bits(right_res.data() + order, block_size - order, right_k);
    result.total_bits = independent_bits;

    // Mid/side estimation
    std::vector<int32_t> mid(block_size);
    std::vector<int32_t> side(block_size);
    for (int i = 0; i < block_size; ++i) {
        int64_t m = (static_cast<int64_t>(left_block[i]) + static_cast<int64_t>(right_block[i])) >> 1;
        int64_t s = static_cast<int64_t>(left_block[i]) - static_cast<int64_t>(right_block[i]);
        mid[i] = static_cast<int32_t>(m);
        side[i] = static_cast<int32_t>(s);
    }

    std::vector<int32_t> mid_res(block_size);
    std::vector<int32_t> side_res(block_size);
    lpc_residual(mid.data(), block_size, {lpc_shift, {}}, order, mid_res.data());
    lpc_residual(side.data(), block_size, {lpc_shift, {}}, order, side_res.data());

    int mid_k = estimate_rice_param(mid_res.data() + order, block_size - order);
    int side_k = estimate_rice_param(side_res.data() + order, block_size - order);

    uint64_t ms_bits = estimate_rice_bits(mid_res.data() + order, block_size - order, mid_k)
                      + estimate_rice_bits(side_res.data() + order, block_size - order, side_k);

    if (ms_bits < independent_bits) {
        result.method = StereoMethod::MidSide;
        result.total_bits = ms_bits;
    }

    return result;
}

}  // namespace

// ── FLAC Encode ──

namespace flac {

auto encode(const std::vector<int16_t>& samples, int sample_rate,
            int num_channels, int bits_per_sample,
            int compression_level) -> std::vector<uint8_t> {
    const size_t total_samples = samples.size() / static_cast<size_t>(num_channels);

    // Determine block size based on compression level
    int block_size;
    switch (compression_level) {
        case 0:  block_size = 1152; break;
        case 1:  block_size = 1152; break;
        case 2:  block_size = 1152; break;
        case 3:  block_size = 2304; break;
        case 4:  block_size = 2304; break;
        case 5:  block_size = 2304; break;
        case 6:  block_size = 4608; break;
        case 7:  block_size = 4608; break;
        default: block_size = 4608; break;
    }

    // LPC order based on compression level
    int max_lpc_order;
    switch (compression_level) {
        case 0: case 1: max_lpc_order = 0; break;
        case 2:          max_lpc_order = 2; break;
        case 3: case 4: max_lpc_order = 4; break;
        case 5:          max_lpc_order = 6; break;
        case 6: case 7: max_lpc_order = 8; break;
        default:        max_lpc_order = 12; break;
    }

    // Stereo method
    bool try_midside = (num_channels == 2 && compression_level >= 3);

    FlacBitWriter w;

    // ── fLaC magic ──
    w.write_bytes(reinterpret_cast<const uint8_t*>("fLaC"), 4);

    // ── STREAMINFO metadata block (34 bytes) ──
    w.write_bits(0, 1);    // not last
    w.write_bits(0, 7);    // STREAMINFO
    w.write_bits(34, 24);  // length

    int actual_min_block = static_cast<int>(total_samples % static_cast<size_t>(block_size));
    if (actual_min_block == 0) actual_min_block = block_size;
    w.write_bits(static_cast<uint32_t>(actual_min_block), 16);  // min block
    w.write_bits(static_cast<uint32_t>(block_size), 16);        // max block
    w.write_bits(0, 24);  // min frame
    w.write_bits(0, 24);  // max frame
    w.write_bits(static_cast<uint32_t>(sample_rate), 20);
    w.write_bits(static_cast<uint32_t>(num_channels - 1), 3);
    w.write_bits(static_cast<uint32_t>(bits_per_sample - 1), 5);
    w.write_bits(static_cast<uint32_t>(total_samples & 0xFFFFFFFFULL), 32);
    w.write_bits(static_cast<uint32_t>((total_samples >> 32) & 0x0FULL), 4);
    for (int i = 0; i < 16; ++i) w.write_bits(0, 8);  // MD5 placeholder

    // ── PADDING metadata (4096 bytes for header editing) ──
    const int padding_size = 4096;
    w.write_bits(0, 1);  // not last
    w.write_bits(1, 7);  // PADDING
    w.write_bits(padding_size, 24);
    for (int i = 0; i < padding_size; ++i) w.write_bits(0, 8);

    const size_t padding_pos = 4 + 4 + 34;

    // ── Sample rate lookup ──
    auto sample_rate_code = [](int sr) -> uint32_t {
        switch (sr) {
            case 88200: return 1;  case 176400: return 2;
            case 192000: return 3; case 8000: return 4;
            case 16000: return 5;  case 22050: return 6;
            case 24000: return 7;  case 32000: return 8;
            case 44100: return 9;  case 48000: return 10;
            case 96000: return 11; default: return 0;
        }
    };
    const uint32_t sr_code = sample_rate_code(sample_rate);

    // ── Block size code lookup ──
    auto block_size_code = [](int bs) -> uint32_t {
        if (bs == 192) return 1;
        for (int i = 2; i <= 5; ++i)
            if (bs == 576 * (1 << (i - 2))) return static_cast<uint32_t>(i);
        if (bs <= 65535) {
            if (bs >= 1024) {
                int n = 8;
                int v = 256;
                while (v < bs && n < 15) { ++n; v <<= 1; }
                return static_cast<uint32_t>(n);
            }
        }
        return static_cast<uint32_t>(13);  // fallback
    };

    const uint32_t default_bs_code = block_size_code(block_size);

    // ── Encode frames ──
    size_t frame_start_sample = 0;
    uint64_t frame_number = 0;

    while (frame_start_sample < total_samples) {
        size_t remaining = total_samples - frame_start_sample;
        int cur_block_size = static_cast<int>(std::min(static_cast<size_t>(block_size), remaining));
        size_t frame_start_byte = w.size();

        // Compute dynamic block size code for this frame
        uint32_t cur_bs_code = (cur_block_size == block_size) ? default_bs_code
            : block_size_code(cur_block_size);

        // ── Frame header ──
        w.write_bits(0x3FFE, 14);  // sync code
        w.write_bits(0, 1);        // reserved
        w.write_bits(0, 1);        // blocking strategy: fixed
        w.write_bits(cur_bs_code, 4);
        w.write_bits(sr_code, 4);

        // Channel assignment
        uint32_t ch_assignment;
        bool use_midside = false;

        if (num_channels == 1) {
            ch_assignment = 0;  // mono
        } else if (num_channels == 2 && try_midside) {
            // Evaluate whether to use mid/side
            std::vector<int32_t> left_block(cur_block_size);
            std::vector<int32_t> right_block(cur_block_size);
            for (int i = 0; i < cur_block_size; ++i) {
                size_t idx = (frame_start_sample + static_cast<size_t>(i)) * 2;
                left_block[i] = samples[idx];
                right_block[i] = samples[idx + 1];
            }

            // Quick energy comparison
            uint64_t left_energy = 0, right_energy = 0, diff_energy = 0;
            for (int i = 0; i < cur_block_size; ++i) {
                left_energy += static_cast<uint64_t>(std::abs(left_block[i]));
                right_energy += static_cast<uint64_t>(std::abs(right_block[i]));
                int64_t diff = static_cast<int64_t>(left_block[i]) - static_cast<int64_t>(right_block[i]);
                diff_energy += static_cast<uint64_t>(std::abs(diff));
            }
            if (diff_energy * 4 < (left_energy + right_energy) * 3) {
                use_midside = true;
            }
        }

        if (num_channels == 2 && !use_midside) {
            ch_assignment = 1;  // independent stereo
        } else if (num_channels == 2 && use_midside) {
            ch_assignment = 8;  // mid/side stereo
        } else if (num_channels > 2) {
            ch_assignment = static_cast<uint32_t>(num_channels - 1);
        } else {
            ch_assignment = 0;
        }

        w.write_bits(ch_assignment, 4);

        // Sample size
        int sample_size_code;
        switch (bits_per_sample) {
            case 8: sample_size_code = 0; break;
            case 12: sample_size_code = 1; break;
            case 16: sample_size_code = 2; break;
            case 20: sample_size_code = 3; break;
            case 24: sample_size_code = 4; break;
            default: sample_size_code = 2; break;
        }
        w.write_bits(static_cast<uint32_t>(sample_size_code), 3);
        w.write_bits(0, 1);  // reserved

        // Frame number
        w.write_utf8(frame_number);

        // If block size doesn't match standard code, write override
        if (cur_bs_code >= 6) {
            if (cur_bs_code == 6) {
                w.write_bits(static_cast<uint32_t>(cur_block_size - 1), 8);
            } else if (cur_bs_code == 7) {
                w.write_bits(static_cast<uint32_t>(cur_block_size - 1), 16);
            }
            // codes 8-15 are implicit
        }

        // If sample rate isn't a standard code, write override
        if (sr_code == 0) {
            w.write_bits(static_cast<uint32_t>(sample_rate), 16);
        }

        // CRC-8 of frame header
        size_t crc8_pos = w.size();
        w.write_byte(0);
        size_t header_bytes_len = w.size() - frame_start_byte - 1;
        uint8_t header_crc = crc8(w.buffer().data() + frame_start_byte, header_bytes_len);
        w.replace_byte(crc8_pos, header_crc);

        // ── Subframes ──
        if (use_midside) {
            // Mid/side stereo: encode mid and side channels
            std::vector<int32_t> mid_block(cur_block_size);
            std::vector<int32_t> side_block(cur_block_size);
            for (int i = 0; i < cur_block_size; ++i) {
                size_t idx = (frame_start_sample + static_cast<size_t>(i)) * 2;
                int64_t m = (static_cast<int64_t>(samples[idx]) + static_cast<int64_t>(samples[idx + 1])) >> 1;
                int64_t s = static_cast<int64_t>(samples[idx]) - static_cast<int64_t>(samples[idx + 1]);
                mid_block[i] = static_cast<int32_t>(m);
                side_block[i] = static_cast<int32_t>(s);
            }

            for (int ch_idx = 0; ch_idx < 2; ++ch_idx) {
                const auto& ch_block = (ch_idx == 0) ? mid_block : side_block;
                // LPC analysis
                std::vector<double> ac(max_lpc_order + 1);
                compute_autocorr(ch_block.data(), cur_block_size, ac.data(), max_lpc_order);

                std::vector<double> lpc_coeffs(max_lpc_order, 0.0);
                std::vector<double> refl(max_lpc_order, 0.0);
                levinson_durbin(ac.data(), max_lpc_order, lpc_coeffs.data(), refl.data());

                auto qlpc = quantize_lpc(lpc_coeffs.data(), max_lpc_order);

                std::vector<int32_t> residual(cur_block_size);
                lpc_residual(ch_block.data(), cur_block_size, qlpc, max_lpc_order, residual.data());

                int k = estimate_rice_param(residual.data() + max_lpc_order, cur_block_size - max_lpc_order);
                auto part = find_best_partition(residual.data() + max_lpc_order,
                                                 cur_block_size - max_lpc_order, k);

                w.write_bits(0, 1);  // zero-padding
                // LPC subframe: type=1xxxxx where xxxxx = order-1
                w.write_bits(0x20 | static_cast<uint32_t>(max_lpc_order - 1), 6);
                w.write_bits(0, 1);  // no wasted bits

                // Warmup samples (uncompressed)
                for (int i = 0; i < max_lpc_order; ++i) {
                    w.write_bits(static_cast<uint32_t>(ch_block[i] & 0xFFFF), bits_per_sample);
                }

                // Quantized LPC coefficients precision (15-bit → 14)
                w.write_bits(14, 4);
                w.write_bits(static_cast<uint32_t>(qlpc.shift), 5);
                for (int i = 0; i < max_lpc_order; ++i) {
                    int32_t qv = qlpc.qlp_coeff[i];
                    w.write_bits(static_cast<uint32_t>(qv & 0x7FFF), 15);
                }

                // Partitioned Rice coding
                write_partitioned_rice(w, residual.data() + max_lpc_order,
                                        cur_block_size - max_lpc_order,
                                        part.best_order, part.best_params);
            }
        } else {
            // Independent channels
            for (int ch = 0; ch < num_channels; ++ch) {
                std::vector<int32_t> ch_block(cur_block_size);
                for (int i = 0; i < cur_block_size; ++i) {
                    size_t idx = (frame_start_sample + static_cast<size_t>(i)) *
                                 static_cast<size_t>(num_channels) + static_cast<size_t>(ch);
                    ch_block[i] = samples[idx];
                }

                // Autocorrelation for LPC analysis
                std::vector<double> ac(max_lpc_order + 1);
                compute_autocorr(ch_block.data(), cur_block_size, ac.data(), max_lpc_order);

                std::vector<double> lpc_coeffs(max_lpc_order);
                std::vector<double> refl(max_lpc_order);
                double lpc_error = levinson_durbin(ac.data(), max_lpc_order,
                                                    lpc_coeffs.data(), refl.data());

                // Also evaluate fixed predictors (orders 0-4) for comparison
                int best_pred_order = 0;
                uint64_t best_pred_energy = std::numeric_limits<uint64_t>::max();
                std::vector<int32_t> best_residual(cur_block_size);
                std::vector<int32_t> tmp_residual(cur_block_size);

                for (int order = 0; order <= 4; ++order) {
                    if (order >= cur_block_size) break;
                    for (int i = 0; i < order; ++i) tmp_residual[i] = ch_block[i];
                    for (int i = order; i < cur_block_size; ++i) {
                        int64_t pred = fixed_predict(order, ch_block.data(), i);
                        tmp_residual[i] = static_cast<int32_t>(
                            static_cast<int64_t>(ch_block[i]) - pred);
                    }
                    uint64_t energy = estimate_residual_energy(cur_block_size - order,
                                                   tmp_residual.data() + order);
                    if (energy < best_pred_energy) {
                        best_pred_energy = energy;
                        best_pred_order = order;
                        best_residual = tmp_residual;
                    }
                }

                // LPC residual for comparison
                if (max_lpc_order > 0) {
                    auto qlpc = quantize_lpc(lpc_coeffs.data(), max_lpc_order);
                    lpc_residual(ch_block.data(), cur_block_size, qlpc,
                                  max_lpc_order, tmp_residual.data());
                    uint64_t lpc_energy = estimate_residual_energy(
                        cur_block_size - max_lpc_order,
                        tmp_residual.data() + max_lpc_order);

                    // LPC overhead: warmup samples + coefficients
                    uint64_t lpc_overhead = static_cast<uint64_t>(max_lpc_order) *
                                            static_cast<uint64_t>(bits_per_sample)
                                          + 4 + 5 + static_cast<uint64_t>(max_lpc_order) *
                                            static_cast<uint64_t>(qlpc.shift + 1);

                    if (lpc_energy + lpc_overhead < best_pred_energy) {
                        best_pred_order = -max_lpc_order;  // negative means LPC
                        best_pred_energy = lpc_energy + lpc_overhead;
                        best_residual = tmp_residual;
                    }
                }

                int warmup_count;
                bool use_lpc = (best_pred_order < 0);
                int actual_order = use_lpc ? (-best_pred_order) : best_pred_order;
                warmup_count = actual_order;

                // Write subframe header
                w.write_bits(0, 1);  // not padding bit

                if (use_lpc) {
                    w.write_bits(0, 1);  // zero-padding
                    w.write_bits(0x20 | static_cast<uint32_t>(actual_order - 1), 6);
                    w.write_bits(0, 1);  // no wasted bits

                    // Warmup samples
                    for (int i = 0; i < actual_order; ++i) {
                        w.write_bits(static_cast<uint32_t>(ch_block[i] & 0xFFFF), bits_per_sample);
                    }

                    // Get quantized LPC
                    auto qlpc = quantize_lpc(lpc_coeffs.data(), actual_order);
                    w.write_bits(14, 4);
                    w.write_bits(static_cast<uint32_t>(qlpc.shift), 5);
                    for (int i = 0; i < actual_order; ++i) {
                        int32_t qv = qlpc.qlp_coeff[i];
                        w.write_bits(static_cast<uint32_t>(qv) & 0x7FFF, 15);
                    }

                    // Rice coding
                    int k = estimate_rice_param(best_residual.data() + actual_order,
                                                 cur_block_size - actual_order);
                    auto part = find_best_partition(best_residual.data() + actual_order,
                                                     cur_block_size - actual_order, k);
                    write_partitioned_rice(w, best_residual.data() + actual_order,
                                            cur_block_size - actual_order,
                                            part.best_order, part.best_params);
                } else {
                    // Fixed predictor subframe: type=001xxx (6 bits)
                    w.write_bits(0, 1);  // zero-padding
                    w.write_bits(0x08 | static_cast<uint32_t>(actual_order), 6);
                    w.write_bits(0, 1);  // no wasted bits

                    // Warmup samples
                    for (int i = 0; i < actual_order; ++i) {
                        w.write_bits(static_cast<uint32_t>(ch_block[i] & 0xFFFF), bits_per_sample);
                    }

                    // Rice coding: partition order opt
                    int k = estimate_rice_param(best_residual.data() + actual_order,
                                                 cur_block_size - actual_order);
                    auto part = find_best_partition(best_residual.data() + actual_order,
                                                     cur_block_size - actual_order, k);
                    write_partitioned_rice(w, best_residual.data() + actual_order,
                                            cur_block_size - actual_order,
                                            part.best_order, part.best_params);
                }
            }
        }

        // ── Frame CRC-16 ──
        size_t frame_end = w.size();
        uint16_t frame_crc = crc16(w.buffer().data() + frame_start_byte,
                                    frame_end - frame_start_byte);
        w.write_byte(static_cast<uint8_t>(frame_crc >> 8));
        w.write_byte(static_cast<uint8_t>(frame_crc & 0xFF));

        frame_start_sample += static_cast<size_t>(cur_block_size);
        ++frame_number;
    }

    // ── Finalize ──
    auto result = w.take_buffer();
    if (padding_pos + 4 + static_cast<size_t>(padding_size) <= result.size()) {
        // Set the PADDING block's is_last flag
        // PADDING header is at padding_pos: 1 bit is_last(0) + 7 bits type(1) + 24 bits len
        const size_t seek_start = 4 + 4 + 34;
        const size_t seek_end = seek_start + 4 + static_cast<size_t>(padding_size);
        if (seek_start < result.size()) {
            result[seek_start] |= 0x80;
        }
    } else {
        result[padding_pos] |= 0x80;
    }

    return result;
}

}  // namespace flac

// ── WAV parsing ──

auto parse_wav(const std::vector<uint8_t>& data) -> WavInfo {
    WavInfo info;
    if (data.size() < 44) return info;

    if (data[0] != 'R' || data[1] != 'I' || data[2] != 'F' || data[3] != 'F')
        return info;
    if (data[8] != 'W' || data[9] != 'A' || data[10] != 'V' || data[11] != 'E')
        return info;

    size_t pos = 12;
    while (pos + 8 <= data.size()) {
        uint32_t chunk_size = static_cast<uint32_t>(data[pos + 4]) |
                              (static_cast<uint32_t>(data[pos + 5]) << 8) |
                              (static_cast<uint32_t>(data[pos + 6]) << 16) |
                              (static_cast<uint32_t>(data[pos + 7]) << 24);
        if (data[pos] == 'f' && data[pos + 1] == 'm' && data[pos + 2] == 't' && data[pos + 3] == ' ') {
            if (pos + 24 > data.size()) return info;
            uint16_t audio_format = static_cast<uint16_t>(data[pos + 8]) |
                                    (static_cast<uint16_t>(data[pos + 9]) << 8);
            if (audio_format != 1) return info;
            info.num_channels = static_cast<int>(data[pos + 10]) |
                                (static_cast<int>(data[pos + 11]) << 8);
            info.sample_rate = static_cast<int>(data[pos + 12]) |
                               (static_cast<int>(data[pos + 13]) << 8) |
                               (static_cast<int>(data[pos + 14]) << 16) |
                               (static_cast<int>(data[pos + 15]) << 24);
            info.bits_per_sample = static_cast<int>(data[pos + 22]) |
                                   (static_cast<int>(data[pos + 23]) << 8);
            pos += 8 + chunk_size;
            if (chunk_size & 1) ++pos;
        } else if (data[pos] == 'd' && data[pos + 1] == 'a' && data[pos + 2] == 't' && data[pos + 3] == 'a') {
            info.data_offset = pos + 8;
            info.data_size = chunk_size;
            break;
        } else {
            pos += 8 + chunk_size;
            if (chunk_size & 1) ++pos;
        }
    }
    return info;
}

auto build_wav(const std::vector<int16_t>& samples, int sample_rate,
               int num_channels, int bits_per_sample) -> std::vector<uint8_t> {
    size_t data_bytes = samples.size() * sizeof(int16_t);
    size_t file_size = 36 + data_bytes;
    std::vector<uint8_t> wav(file_size + 8, 0);

    auto put32 = [&](size_t off, uint32_t val) {
        wav[off] = static_cast<uint8_t>(val & 0xFF);
        wav[off + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
        wav[off + 2] = static_cast<uint8_t>((val >> 16) & 0xFF);
        wav[off + 3] = static_cast<uint8_t>((val >> 24) & 0xFF);
    };
    auto put16 = [&](size_t off, uint16_t val) {
        wav[off] = static_cast<uint8_t>(val & 0xFF);
        wav[off + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
    };

    wav[0] = 'R'; wav[1] = 'I'; wav[2] = 'F'; wav[3] = 'F';
    put32(4, static_cast<uint32_t>(file_size));
    wav[8] = 'W'; wav[9] = 'A'; wav[10] = 'V'; wav[11] = 'E';
    wav[12] = 'f'; wav[13] = 'm'; wav[14] = 't'; wav[15] = ' ';
    put32(16, 16);
    put16(20, 1);
    put16(22, static_cast<uint16_t>(num_channels));
    put32(24, static_cast<uint32_t>(sample_rate));
    put32(28, static_cast<uint32_t>(sample_rate * num_channels * bits_per_sample / 8));
    put16(32, static_cast<uint16_t>(num_channels * bits_per_sample / 8));
    put16(34, static_cast<uint16_t>(bits_per_sample));
    wav[36] = 'd'; wav[37] = 'a'; wav[38] = 't'; wav[39] = 'a';
    put32(40, static_cast<uint32_t>(data_bytes));

    std::memcpy(wav.data() + 44, samples.data(), data_bytes);
    return wav;
}

// ── Public API ──

auto audio_compress(const std::vector<uint8_t>& wav_data,
                    AudioFormat format, int quality) -> std::vector<uint8_t> {
    auto info = parse_wav(wav_data);
    if (info.sample_rate == 0 || info.data_size == 0)
        return {};

    size_t sample_count = info.data_size / (info.bits_per_sample / 8);
    std::vector<int16_t> samples(sample_count);

    if (info.bits_per_sample == 16) {
        std::memcpy(samples.data(), wav_data.data() + info.data_offset,
                    info.data_size);
    } else if (info.bits_per_sample == 8) {
        for (size_t i = 0; i < sample_count; ++i) {
            int8_t val = static_cast<int8_t>(wav_data[info.data_offset + i]);
            samples[i] = static_cast<int16_t>(static_cast<int>(val) << 8);
        }
    } else {
        return {};
    }

    switch (format) {
        case AudioFormat::FLAC:
            return flac::encode(samples, info.sample_rate,
                                info.num_channels, info.bits_per_sample, quality);
    }
    return {};
}

auto audio_decompress(const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
    if (data.size() < 4) return {};
    if (data[0] != 'f' || data[1] != 'L' || data[2] != 'a' || data[3] != 'C')
        return {};
    return flac::decode(data);
}

// ── FLAC Decoder ──

namespace flac {

namespace {

int32_t fixed_warmup_predict(int order, const int32_t* warmup, int pos) {
    return static_cast<int32_t>(fixed_predict(order, warmup, pos));
}

// Read Rice-coded residual from bitstream
int32_t read_rice_residual(const uint8_t* buf, size_t& byte_pos, int& bit_off,
                            int k, size_t buf_size) {
    auto read_bit = [&]() -> int {
        if (byte_pos >= buf_size) return 0;
        int b = (buf[byte_pos] >> (7 - bit_off)) & 1;
        bit_off++;
        if (bit_off >= 8) { bit_off = 0; byte_pos++; }
        return b;
    };

    auto read_bits = [&](int n) -> uint32_t {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i)
            v = (v << 1) | static_cast<uint32_t>(read_bit());
        return v;
    };

    uint32_t q = 0;
    while (read_bit() == 1) ++q;
    uint32_t r = (k > 0) ? read_bits(k) : 0;
    uint32_t u = (q << k) | r;
    if (u & 1) return -static_cast<int32_t>((u + 1) >> 1);
    return static_cast<int32_t>(u >> 1);
}

}  // namespace

auto decode(const std::vector<uint8_t>& flac_data) -> std::vector<uint8_t> {
    if (flac_data.size() < 42) return {};
    if (flac_data[0] != 'f' || flac_data[1] != 'L' ||
        flac_data[2] != 'a' || flac_data[3] != 'C')
        return {};

    // Parse STREAMINFO
    uint32_t sr_raw = (static_cast<uint32_t>(flac_data[16]) << 12) |
                      (static_cast<uint32_t>(flac_data[17]) << 4) |
                      (static_cast<uint32_t>(flac_data[18]) >> 4);
    int sample_rate = static_cast<int>(sr_raw & 0xFFFFF);
    int num_channels = static_cast<int>((flac_data[18] >> 1) & 0x07) + 1;
    int bits_per_sample = static_cast<int>(((flac_data[18] & 0x01) << 4) |
                                            (flac_data[19] >> 4)) + 1;

    uint64_t total_samples = (static_cast<uint64_t>(flac_data[20] & 0x0F) << 32) |
                             (static_cast<uint64_t>(flac_data[21]) << 24) |
                             (static_cast<uint64_t>(flac_data[22]) << 16) |
                             (static_cast<uint64_t>(flac_data[23]) << 8) |
                             static_cast<uint64_t>(flac_data[24]);

    // Skip metadata blocks to get to audio frames
    size_t pos = 4;
    while (pos + 4 <= flac_data.size()) {
        bool is_last = (flac_data[pos] & 0x80) != 0;
        uint32_t block_len = (static_cast<uint32_t>(flac_data[pos + 1]) << 16) |
                             (static_cast<uint32_t>(flac_data[pos + 2]) << 8) |
                             static_cast<uint32_t>(flac_data[pos + 3]);
        pos += 4 + block_len;
        if (is_last) break;
    }

    if (pos >= flac_data.size()) return {};

    std::vector<int16_t> all_samples;
    std::vector<std::vector<double>> overlap(num_channels,
                                              std::vector<double>(1024, 0.0));

    while (pos + 2 < flac_data.size()) {
        // Find sync code 0xFFF8-0xFFFE
        while (pos + 1 < flac_data.size()) {
            if (flac_data[pos] == 0xFF && (flac_data[pos + 1] & 0xFC) == 0xF8)
                break;
            ++pos;
        }
        if (pos + 2 >= flac_data.size()) break;

        size_t frame_start = pos;
        size_t pos_bytes = pos + 2;  // skip sync code

        // Parse frame header header
        uint8_t b1 = flac_data[frame_start + 1];
        uint32_t bs_code_raw = (b1 >> 4) & 0x0F;
        uint8_t b2 = flac_data[frame_start + 2];
        int ch_assign = (b2 >> 4) & 0x0F;
        int bps_code = (b2 >> 1) & 0x07;

        int actual_channels;
        bool midside = false;
        if (ch_assign < 8) actual_channels = ch_assign + 1;
        else { actual_channels = 2; if (ch_assign >= 8 && ch_assign <= 10) midside = true; }

        // Decode block size
        int block_sz;
        switch (bs_code_raw) {
            case 1: block_sz = 192; break;
            case 2: case 3: case 4: case 5:
                block_sz = 576 << (bs_code_raw - 2); break;
            default: block_sz = 256 << (bs_code_raw - 8); break;
        }

        // Skip to after CRC-8: sync(2) + header(1) + ch/bps(1) + utf8 + opts + crc8(1)
        pos_bytes = frame_start + 3;
        uint8_t utf8_first = flac_data[pos_bytes];
        int utf8_len = 1;
        if (utf8_first >= 0xE0 && utf8_first < 0xF0) utf8_len = 3;
        else if (utf8_first >= 0xC0 && utf8_first < 0xE0) utf8_len = 2;
        else if (utf8_first >= 0x80 && utf8_first < 0xC0) {}  // continue
        pos_bytes += utf8_len;

        if (bs_code_raw == 6) { block_sz = flac_data[pos_bytes] + 1; pos_bytes++; }
        else if (bs_code_raw == 7) {
            block_sz = (flac_data[pos_bytes] << 8) | flac_data[pos_bytes + 1]; pos_bytes += 2;
        }

        pos_bytes++;  // CRC-8
        if (pos_bytes >= flac_data.size()) break;

        // Decode subframes
        std::vector<std::vector<int32_t>> ch_data(actual_channels,
                                                    std::vector<int32_t>(block_sz));

        for (int ch = 0; ch < actual_channels; ++ch) {
            if (pos_bytes >= flac_data.size()) break;

            uint8_t sf_byte = flac_data[pos_bytes++];
            int sf_type = (sf_byte >> 1) & 0x3F;

            if (sf_type == 0) {
                // CONSTANT
                int32_t const_val = 0;
                if (bits_per_sample == 16 && pos_bytes + 2 <= flac_data.size()) {
                    const_val = static_cast<int16_t>(
                        (flac_data[pos_bytes] << 8) | flac_data[pos_bytes + 1]);
                    pos_bytes += 2;
                } else if (bits_per_sample == 8 && pos_bytes < flac_data.size()) {
                    const_val = static_cast<int8_t>(flac_data[pos_bytes++]);
                }
                for (int i = 0; i < block_sz; ++i) ch_data[ch][i] = const_val;
            } else if (sf_type >= 8 && sf_type <= 12) {
                // FIXED
                int order = sf_type - 8;
                std::vector<int32_t> warmup(order);
                for (int i = 0; i < order; ++i) {
                    if (bits_per_sample == 16 && pos_bytes + 2 <= flac_data.size()) {
                        warmup[i] = static_cast<int16_t>(
                            (flac_data[pos_bytes] << 8) | flac_data[pos_bytes + 1]);
                        pos_bytes += 2;
                    } else if (bits_per_sample == 8 && pos_bytes < flac_data.size()) {
                        warmup[i] = static_cast<int8_t>(flac_data[pos_bytes++]);
                    }
                }

                // Rice coding: method(1b) + k(3b) + partition_order(4b)
                int bit_off = 0;
                size_t bp = pos_bytes;
                auto read_bit_local = [&]() -> int {
                    if (bp >= flac_data.size()) return 0;
                    int b = (flac_data[bp] >> (7 - bit_off)) & 1;
                    bit_off++;
                    if (bit_off >= 8) { bit_off = 0; bp++; }
                    return b;
                };
                auto read_bits_local = [&](int n) -> uint32_t {
                    uint32_t v = 0;
                    for (int i = 0; i < n; ++i)
                        v = (v << 1) | static_cast<uint32_t>(read_bit_local());
                    return v;
                };

                int method = read_bit_local();
                if (method != 0) { pos_bytes = pos + 2; break; }  // unsupported
                int rice_k = static_cast<int>(read_bits_local(3));
                int part_order = static_cast<int>(read_bits_local(4));

                // Read per-partition Rice params
                int num_parts = 1 << part_order;
                size_t total_res = static_cast<size_t>(block_sz) - static_cast<size_t>(order);
                size_t base_cnt = total_res / static_cast<size_t>(num_parts);
                size_t extra_cnt = total_res % static_cast<size_t>(num_parts);

                std::vector<int32_t> residuals(total_res);
                size_t res_idx = 0;
                for (int p = 0; p < num_parts && res_idx < total_res; ++p) {
                    int pk = static_cast<int>(read_bits_local(4));
                    size_t cnt = base_cnt + (static_cast<size_t>(p) < extra_cnt ? 1 : 0);
                    for (size_t i = 0; i < cnt && res_idx < total_res; ++i) {
                        uint32_t q = 0;
                        while (read_bit_local() == 1 && bp < flac_data.size()) ++q;
                        uint32_t r = (pk > 0) ? read_bits_local(pk) : 0;
                        uint32_t u = (q << pk) | r;
                        if (u & 1) residuals[res_idx] = -static_cast<int32_t>((u + 1) >> 1);
                        else residuals[res_idx] = static_cast<int32_t>(u >> 1);
                        ++res_idx;
                    }
                }

                pos_bytes = bp + (bit_off > 0 ? 1 : 0);

                for (int i = 0; i < order; ++i) ch_data[ch][i] = warmup[i];
                for (size_t i = 0; i < total_res; ++i) {
                    size_t sample_i = static_cast<size_t>(order) + i;
                    int64_t pred = fixed_predict(order, ch_data[ch].data(),
                                                  static_cast<int>(sample_i));
                    ch_data[ch][sample_i] = static_cast<int32_t>(
                        static_cast<int64_t>(residuals[i]) + pred);
                }
            } else if (sf_type == 1) {
                // VERBATIM
                for (int i = 0; i < block_sz; ++i) {
                    if (pos_bytes + 2 <= flac_data.size()) {
                        ch_data[ch][i] = static_cast<int16_t>(
                            (flac_data[pos_bytes] << 8) | flac_data[pos_bytes + 1]);
                        pos_bytes += 2;
                    }
                }
            } else if (sf_type >= 32) {
                // LPC: type=1 (32-63 range), order = sf_type - 31
                int order = sf_type - 31;
                std::vector<int32_t> warmup(order);
                for (int i = 0; i < order; ++i) {
                    if (bits_per_sample == 16 && pos_bytes + 2 <= flac_data.size()) {
                        warmup[i] = static_cast<int16_t>(
                            (flac_data[pos_bytes] << 8) | flac_data[pos_bytes + 1]);
                        pos_bytes += 2;
                    }
                }

                int bit_off = 0;
                size_t bp = pos_bytes;
                auto read_bit_local_lpc = [&]() -> int {
                    if (bp >= flac_data.size()) return 0;
                    int b = (flac_data[bp] >> (7 - bit_off)) & 1;
                    bit_off++;
                    if (bit_off >= 8) { bit_off = 0; bp++; }
                    return b;
                };
                auto read_bits_local_lpc = [&](int n) -> uint32_t {
                    uint32_t v = 0;
                    for (int i = 0; i < n; ++i)
                        v = (v << 1) | static_cast<uint32_t>(read_bit_local_lpc());
                    return v;
                };

                int qprec = static_cast<int>(read_bits_local_lpc(4));
                int qprec2 = static_cast<int>(read_bits_local_lpc(5));  // FLAC spec: 4+5 bits for prec
                int precision = qprec;  // use the 4-bit value

                std::vector<int32_t> qlp_coeff(order);
                for (int i = 0; i < order; ++i) {
                    int32_t val = static_cast<int32_t>(read_bits_local_lpc(precision + 1));
                    if (val >= (1 << precision)) val -= (1 << (precision + 1));
                    qlp_coeff[i] = val;
                }

                // Residual
                int rice_method = read_bit_local_lpc();
                if (rice_method != 0) { pos_bytes = pos + 2; break; }
                int rice_k = static_cast<int>(read_bits_local_lpc(3));
                int part_order = static_cast<int>(read_bits_local_lpc(4));

                int num_parts = 1 << part_order;
                size_t total_res = static_cast<size_t>(block_sz) - static_cast<size_t>(order);
                size_t base_cnt = total_res / static_cast<size_t>(num_parts);
                size_t extra_cnt = total_res % static_cast<size_t>(num_parts);

                std::vector<int32_t> residuals(total_res);
                size_t res_idx = 0;
                for (int p = 0; p < num_parts && res_idx < total_res; ++p) {
                    int pk = static_cast<int>(read_bits_local_lpc(4));
                    size_t cnt = base_cnt + (static_cast<size_t>(p) < extra_cnt ? 1 : 0);
                    for (size_t i = 0; i < cnt && res_idx < total_res; ++i) {
                        uint32_t q = 0;
                        while (read_bit_local_lpc() == 1 && bp < flac_data.size()) ++q;
                        uint32_t r = (pk > 0) ? read_bits_local_lpc(pk) : 0;
                        uint32_t u = (q << pk) | r;
                        if (u & 1) residuals[res_idx] = -static_cast<int32_t>((u + 1) >> 1);
                        else residuals[res_idx] = static_cast<int32_t>(u >> 1);
                        ++res_idx;
                    }
                }

                pos_bytes = bp + (bit_off > 0 ? 1 : 0);

                // Reconstruct LPC
                for (int i = 0; i < order; ++i) ch_data[ch][i] = warmup[i];
                for (int i = order; i < block_sz; ++i) {
                    int64_t pred = 0;
                    for (int j = 0; j < order; ++j)
                        pred += static_cast<int64_t>(qlp_coeff[j]) *
                                static_cast<int64_t>(ch_data[ch][i - 1 - j]);
                    pred >>= precision;
                    size_t r_idx = static_cast<size_t>(i - order);
                    ch_data[ch][i] = static_cast<int32_t>(
                        static_cast<int64_t>(residuals[r_idx]) + pred);
                }
            } else {
                // Unsupported subframe type - skip this frame
                pos_bytes = pos + 2;
                break;
            }
        }

        // Skip CRC-16
        pos_bytes += 2;

        // Interleave decoded samples
        for (int i = 0; i < block_sz; ++i) {
            for (int ch = 0; ch < actual_channels; ++ch) {
                int32_t sample = ch_data[ch][i];
                sample = std::max(-0x8000, std::min(0x7FFF, static_cast<int>(sample)));
                all_samples.push_back(static_cast<int16_t>(sample));
            }
        }

        pos = pos_bytes;
    }

    return build_wav(all_samples, sample_rate, num_channels, bits_per_sample);
}

}  // namespace flac

}  // namespace compressor::algorithm
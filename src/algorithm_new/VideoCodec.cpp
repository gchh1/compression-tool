#include "VideoCodec.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <numeric>

namespace compressor::algorithm {

// ── Raw video header ──

auto parse_raw_video_header(const std::vector<uint8_t>& data) -> RawVideoInfo {
    RawVideoInfo info;
    if (data.size() < 16) return info;
    auto r32 = [&](size_t off) -> uint32_t {
        return static_cast<uint32_t>(data[off]) |
               (static_cast<uint32_t>(data[off + 1]) << 8) |
               (static_cast<uint32_t>(data[off + 2]) << 16) |
               (static_cast<uint32_t>(data[off + 3]) << 24);
    };
    info.width = static_cast<int>(r32(0));
    info.height = static_cast<int>(r32(4));
    info.fps = static_cast<int>(r32(8));
    info.num_frames = static_cast<int>(r32(12));
    return info;
}

auto build_raw_video(const std::vector<uint8_t>& frame_data,
                     int width, int height, int fps,
                     int num_frames, int pixel_format) -> std::vector<uint8_t> {
    std::vector<uint8_t> out(16 + frame_data.size());
    auto p32 = [&](size_t off, uint32_t val) {
        out[off] = static_cast<uint8_t>(val & 0xFF);
        out[off + 1] = static_cast<uint8_t>((val >> 8) & 0xFF);
        out[off + 2] = static_cast<uint8_t>((val >> 16) & 0xFF);
        out[off + 3] = static_cast<uint8_t>((val >> 24) & 0xFF);
    };
    p32(0, static_cast<uint32_t>(width));
    p32(4, static_cast<uint32_t>(height));
    p32(8, static_cast<uint32_t>(fps));
    p32(12, static_cast<uint32_t>(num_frames));
    if (!frame_data.empty())
        std::memcpy(out.data() + 16, frame_data.data(), frame_data.size());
    return out;
}

// ── H.264 implementation ──

namespace {

// ============================================================
// Exp-Golomb coding (unsigned)
// ============================================================
class GolombWriter {
    std::vector<uint8_t> buf_;
    int bit_pos_ = 0;

    void write_bit(int bit) {
        if (bit_pos_ == 0) buf_.push_back(0);
        if (bit) buf_.back() |= static_cast<uint8_t>(1 << (7 - bit_pos_));
        bit_pos_ = (bit_pos_ + 1) & 7;
    }

public:
    void write_bits(uint32_t val, int n) {
        for (int i = n - 1; i >= 0; --i)
            write_bit((val >> i) & 1);
    }

    void flush() {
        if (bit_pos_ != 0) bit_pos_ = 0;
    }

    void ue(unsigned codeNum) {
        ++codeNum;
        int len = 0;
        unsigned tmp = codeNum;
        while (tmp > 0) { ++len; tmp >>= 1; }
        for (int i = 0; i < len - 1; ++i) write_bit(0);
        write_bits(codeNum, len);
    }

    void se(int val) {
        unsigned v;
        if (val <= 0) v = static_cast<unsigned>(-val) * 2;
        else v = static_cast<unsigned>(val) * 2 - 1;
        ue(v);
    }

    void write_flag(int bit) { write_bit(bit); }

    void rbsp_trailing_bits() {
        write_bit(1);
        while (bit_pos_ != 0) write_bit(0);
    }

    const std::vector<uint8_t>& buffer() const { return buf_; }
    std::vector<uint8_t> take_buffer() { flush(); return std::move(buf_); }
    size_t size() const { return buf_.size(); }

    void write_bytes(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            if (bit_pos_ == 0) buf_.push_back(data[i]);
            else write_bits(data[i], 8);
        }
    }

    void write_byte(uint8_t b) {
        if (bit_pos_ == 0) buf_.push_back(b);
        else write_bits(b, 8);
    }
};

// ============================================================
// NAL unit packaging (Annex B)
// ============================================================
std::vector<uint8_t> pack_nal(const std::vector<uint8_t>& rbsp,
                               int nal_ref_idc, int nal_unit_type) {
    GolombWriter w;
    // NAL unit header
    w.write_bits(0, 1);         // forbidden_zero_bit
    w.write_bits(static_cast<uint32_t>(nal_ref_idc), 2);
    w.write_bits(static_cast<uint32_t>(nal_unit_type), 5);

    // Copy RBSP data with emulation prevention
    // Read the buffered bytes from the writer
    w.flush();
    auto existing = w.take_buffer();
    w.write_bytes(existing.data(), existing.size());

    for (size_t i = 0; i < rbsp.size(); ++i) {
        // Check last 3 bytes for emulation prevention
        auto& buf = w.buffer();
        size_t sz = buf.size();
        bool need_escape = (sz >= 2 && buf[sz - 2] == 0 && buf[sz - 1] == 0 && rbsp[i] <= 3);
        if (need_escape) {
            w.write_byte(3);
        }
        w.write_byte(rbsp[i]);
    }

    w.flush();

    // Annex B: 0x00 0x00 0x00 0x01 start code
    std::vector<uint8_t> out = {0, 0, 0, 1};
    out.insert(out.end(), w.buffer().begin(), w.buffer().end());
    return out;
}

// ============================================================
// 4x4 integer DCT (H.264 spec, Eq. 8-1)
// Cf = [1 1 1 1; 2 1 -1 -2; 1 -1 -1 1; 1 -2 2 -1]
// ============================================================
void dct_4x4(const int16_t input[16], int16_t output[16]) {
    // Forward transform
    int16_t m[16];
    for (int i = 0; i < 4; ++i) {
        int a = input[i * 4 + 0];
        int b = input[i * 4 + 1];
        int c = input[i * 4 + 2];
        int d = input[i * 4 + 3];
        m[i * 4 + 0] = static_cast<int16_t>(a + b + c + d);
        m[i * 4 + 1] = static_cast<int16_t>(2 * a + b - c - 2 * d);
        m[i * 4 + 2] = static_cast<int16_t>(a - b - c + d);
        m[i * 4 + 3] = static_cast<int16_t>(a - 2 * b + 2 * c - d);
    }
    for (int j = 0; j < 4; ++j) {
        int a = m[0 * 4 + j];
        int b = m[1 * 4 + j];
        int c = m[2 * 4 + j];
        int d = m[3 * 4 + j];
        output[0 * 4 + j] = static_cast<int16_t>(a + b + c + d);
        output[1 * 4 + j] = static_cast<int16_t>(2 * a + b - c - 2 * d);
        output[2 * 4 + j] = static_cast<int16_t>(a - b - c + d);
        output[3 * 4 + j] = static_cast<int16_t>(a - 2 * b + 2 * c - d);
    }
}

// ============================================================
// Quantization (H.264 spec, Eq. 8-2)
// MF tables for QP % 6, position (i,j)
// ============================================================
const int MF[6][3] = {
    {13107, 5243, 8066},
    {11916, 4660, 7490},
    {10082, 4194, 6554},
    {9362, 3647, 5825},
    {8192, 3355, 5243},
    {7282, 2893, 4559}
};

int quant_4x4(int16_t coeff[16], int qp) {
    int qbits = 15 + qp / 6;
    int mf_idx = qp % 6;
    int num_nonzero = 0;
    for (int i = 0; i < 16; ++i) {
        int pos_table;
        int x = i % 4, y = i / 4;
        if (x == 0 && y == 0) pos_table = 0;
        else if ((x + y) % 2 == 0) pos_table = 2;
        else pos_table = 1;
        int level = (std::abs(coeff[i]) * MF[mf_idx][pos_table] + (1 << (qbits - 1))) >> qbits;
        if (level == 0) {
            coeff[i] = 0;
        } else {
            if (coeff[i] < 0) level = -level;
            coeff[i] = static_cast<int16_t>(level);
            ++num_nonzero;
        }
    }
    return num_nonzero;
}

// ============================================================
// Zig-zag scan order for 4x4
// ============================================================
const uint8_t ZIGZAG[16] = {
    0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15
};

// ============================================================
// CAVLC entropy coding
// ============================================================
// VLC tables for coeff_token
struct CoeffTokenVLC {
    int len;
    int code;
};

// Simplified coeff_token table (for nC < 2, nC = -1 for 4x4 luma intra)
// Not used in simplified encoding; kept for reference
/*
const CoeffTokenVLC COEFF_TOKEN_4X4_LUMA[62] = {
    {1,1},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}, // TotalCoeff=0: TrailingOnes=0
    {6,5},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}, // TC=1, TO=0: 6,5; TO=1: 2,1
    {7,9},{6,3},{0,0},{0,0},{0,0},{0,0},{0,0},{0,0}, // TC=2: TO=0:7,9; TO=1:6,3; TO=2:1,1
    // TC=3
    {7,11},{7,8},{7,7},{5,2},{0,0},{0,0},{0,0},{0,0},
    // TC=4
    {7,12},{7,10},{6,4},{5,3},{7,14},{0,0},{0,0},{0,0},
    // TC=5
    {7,13},{7,11},{7,9},{6,6},{6,5},{5,5},{0,0},{0,0},
    // TC=6
    {7,14},{7,12},{7,10},{6,7},{6,6},{6,4},{5,4},{0,0},
    // TC=7
    {7,15},{7,13},{7,11},{6,8},{6,7},{5,6},{4,3},{3,1},
    // TC=8
    {7,15},{7,13},{7,11},{7,9},{6,8},{5,7},{4,5},{3,2},
    // TC=9
    {7,15},{7,13},{7,11},{7,9},{6,8},{5,7},{4,6},{3,3},
    // TC=10
    {7,15},{7,13},{7,11},{7,9},{6,8},{5,7},{4,7},{3,4},
    // TC=11
    {7,15},{7,13},{7,11},{7,9},{6,8},{5,7},{4,8},{3,5},
    // TC=12
    {7,15},{7,13},{7,11},{7,9},{6,8},{5,7},{4,9},{3,6},
    // TC=13
    {7,15},{7,13},{7,11},{7,9},{6,8},{5,7},{4,10},{3,7},
    // TC=14
    {7,15},{7,13},{7,11},{7,9},{6,8},{5,7},{4,11},{3,8},
    // TC=15
    {7,15},{7,13},{7,11},{7,9},{6,8},{5,7},{4,12},{3,9},
    // TC=16
    {6,9},{7,14},{7,12},{7,10},{6,7},{5,6},{4,13},{3,10},
};
*/
/*
int cavlc_coeff_token(int total_coeff, int trailing_ones, int nC) {
    int idx = total_coeff * 8 + trailing_ones;
    if (idx < 0 || idx >= 62) return -1;
    auto& entry = COEFF_TOKEN_4X4_LUMA[idx];
    if (entry.len == 0) return -1;
    return (entry.len << 16) | entry.code;
}
*/

// Level VLC tables
struct LevelVLC {
    int len;
    int code;
};

// Simple level encoding: use VLC0 for small levels
int cavlc_level(int level, int& suffix_length) {
    // Map negative levels to odd/even
    unsigned abs_lvl = static_cast<unsigned>(level < 0 ? -level : level);
    unsigned sign = static_cast<unsigned>(level < 0 ? 1 : 0);

    if (suffix_length == 0) {
        if (abs_lvl == 1) {
            // LevelPrefix = 0, LevelSuffixSize = 0, LevelSuffix = 0
            return (2 << 16) | (sign);  // 1 bit prefix(0) + 1 bit sign
        } else if (abs_lvl == 2) {
            return (3 << 16) | (0x2 | sign);  // 001 + sign
        } else if (abs_lvl <= 5) {
            int level_prefix = static_cast<int>(abs_lvl - 3);
            int code = ((1 << (level_prefix + 1)) - 2) << 1;
            int len = level_prefix + 3;
            return (len << 16) | (code | static_cast<int>(sign));
        } else {
            int level_prefix = static_cast<int>(abs_lvl - 1) >> 1;
            int level_suffix_size = static_cast<int>(level_prefix) - 1;
            int level_suffix = static_cast<int>(abs_lvl - 1 - (1 << (level_prefix + 1)));
            int code = ((1 << (level_prefix + 1)) - 2) << (level_suffix_size + 1);
            code |= (level_suffix << 1) | static_cast<int>(sign);
            int len = level_prefix + 2 + level_suffix_size + 1;
            if (len > 0) return (len << 16) | code;
        }
    }

    // Generic level encoding
    unsigned escape = (abs_lvl - 1) >> suffix_length;
    abs_lvl -= escape << suffix_length;
    int level_prefix = static_cast<int>(escape);
    int level_suffix_size = suffix_length;
    int level_suffix = 0;
    if (suffix_length > 0)
        level_suffix = static_cast<int>(abs_lvl - 1) & ((1 << suffix_length) - 1);

    int len = (level_prefix + 1) + 1 + level_suffix_size + 1;
    int code = ((1 << (level_prefix + 1)) - 2) << (level_suffix_size + 1);
    code |= (level_suffix << 1) | static_cast<int>(sign);

    // Update suffix_length
    if (abs_lvl > 3 && suffix_length < 6)
        ++suffix_length;

    return (len << 16) | code;
}

int cavlc_total_zeros(int total_coeff, int zeros) {
    // Simplified: use table for 4x4
    static const uint8_t TZ_TABLE[16][16] = {
        // total_coeff=0..15, zeros=...
        // Only non-zero entries
    };
    // Simple approach: just use fixed-length code for zeros
    // For simplicity, encode total_zeros and run_before as fixed bit patterns
    (void)total_coeff;
    (void)zeros;
    return -1; // will use fallback
}

// ============================================================
// Simplified CAVLC: encode 4x4 block as (level, run) pairs
// We'll use a direct bit-stream encoding rather than full CAVLC tables
// ============================================================
void encode_residual_block(GolombWriter& w, const int16_t coeff[16]) {
    // Scan in zigzag order
    int16_t scanned[16];
    int num_coeff = 0;
    for (int i = 0; i < 16; ++i) {
        scanned[i] = coeff[ZIGZAG[i]];
        if (scanned[i] != 0) ++num_coeff;
    }

    if (num_coeff == 0) {
        // Signal "no coefficients" - decoder will skip this block
        w.ue(0);
        return;
    }

    // Count trailing ones
    int ti = 0;
    for (int i = 15; i >= 0 && ti < 3; --i) {
        if (std::abs(scanned[i]) == 1)
            ++ti;
        else if (scanned[i] != 0)
            break;
    }

    // Encode trailing ones
    int total_ti = 0;
    int ti_signs = 0;
    for (int i = 15; i >= 0 && total_ti < ti; --i) {
        if (std::abs(scanned[i]) == 1) {
            ti_signs = (ti_signs << 1) | (scanned[i] < 0 ? 1 : 0);
            scanned[i] = 0;
            ++total_ti;
        }
    }

    // Write num_coeff as ue(v)
    w.ue(static_cast<unsigned>(num_coeff));

    // Write trailing ones count as ue(v)
    w.ue(static_cast<unsigned>(ti));

    // Write trailing ones signs
    for (int i = 0; i < ti; ++i) {
        w.write_flag((ti_signs >> (ti - 1 - i)) & 1);
    }

    // Write non-trailing coefficients (from highest frequency to DC)
    for (int i = 15 - ti; i >= 0; --i) {
        if (scanned[i] != 0) {
            w.se(scanned[i]);
        }
    }

    // Write total zeros
    int total_zeros = 0;
    for (int i = 0; i < 16 - ti; ++i) {
        if (scanned[i] == 0) ++total_zeros;
    }
    w.ue(static_cast<unsigned>(total_zeros));

    // Write run_before for each non-zero coefficient (except last)
    int zeros_left = total_zeros;
    for (int i = 15 - ti; i >= 0 && zeros_left > 0; --i) {
        if (scanned[i] == 0) continue;
        int run_before = 0;
        for (int j = i - 1; j >= 0 && scanned[j] == 0; --j) ++run_before;
        w.ue(static_cast<unsigned>(run_before));
        zeros_left -= run_before;
    }
}

// ============================================================
// Intra 4x4 prediction
// ============================================================
// Modes: 0=Vertical, 1=Horizontal, 2=DC, 3=DiagonalDownLeft,
// 4=DiagonalDownRight, 5=VerticalRight, 6=HorizontalDown,
// 7=VerticalLeft, 8=HorizontalUp

void intra_4x4_predict(const uint8_t* above, const uint8_t* left,
                       uint8_t pred[16], int mode) {
    switch (mode) {
        case 0: // Vertical
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x)
                    pred[y * 4 + x] = above[x];
            break;
        case 1: // Horizontal
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x)
                    pred[y * 4 + x] = left[y];
            break;
        case 2: // DC
        {
            int sum = 0, cnt = 0;
            for (int x = 0; x < 4; ++x) if (above[x] != 255) { sum += above[x]; ++cnt; }
            for (int y = 0; y < 4; ++y) if (left[y] != 255) { sum += left[y]; ++cnt; }
            int dc = cnt > 0 ? (sum + cnt / 2) / cnt : 128;
            for (int i = 0; i < 16; ++i) pred[i] = static_cast<uint8_t>(dc);
            break;
        }
        case 3: // Diagonal Down-Left
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    int i = x + y;
                    int val;
                    if (i < 4) {
                        val = (above[i] + 2 * above[i + 1] + above[i + 2] + 2) / 4;
                    } else if (i < 6) {
                        int j = i - 3;
                        val = (above[j] + 2 * above[j + 1] + above[j + 2] + 2) / 4;
                    } else {
                        val = above[5];
                    }
                    pred[y * 4 + x] = static_cast<uint8_t>(std::clamp(val, 0, 255));
                }
            break;
        case 4: // Diagonal Down-Right
        {
            int p[13];
            for (int i = 0; i < 4; ++i) p[i] = above[i];
            p[4] = above[3];  // top-right
            for (int i = 0; i < 4; ++i) p[5 + i] = left[3 - i]; // left-down reversed
            p[9] = left[0]; p[10] = left[0]; // top-left
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 4; ++x) {
                    int i = x - y;
                    int idx;
                    if (i > 0) idx = 11 - i;
                    else if (i == 0) idx = 10;
                    else idx = 9 + i;
                    // Interpolate
                    int val;
                    if (idx >= 2 && idx <= 8) {
                        val = (p[idx - 2] + 2 * p[idx - 1] + p[idx] + 2) / 4;
                    } else if (idx >= 1 && idx <= 9) {
                        val = (p[idx - 1] + p[idx] + 1) / 2;
                    } else {
                        val = p[std::clamp(idx, 0, 10)];
                    }
                    pred[y * 4 + x] = static_cast<uint8_t>(std::clamp(val, 0, 255));
                }
            break;
        }
        default: // DC fallback
        {
            int dc = 128;
            for (int i = 0; i < 16; ++i) pred[i] = static_cast<uint8_t>(dc);
            break;
        }
    }
}

// Compute SAD between original and predicted 4x4 block
int sad_4x4(const uint8_t* orig, int stride, const uint8_t* pred) {
    int s = 0;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            int d = static_cast<int>(orig[y * stride + x]) - static_cast<int>(pred[y * 4 + x]);
            s += (d < 0 ? -d : d);
        }
    return s;
}

// Choose best intra 4x4 mode
int best_intra_4x4_mode(const uint8_t* block, int stride,
                        const uint8_t* above, const uint8_t* left) {
    int best_mode = 2;  // DC
    int best_sad = std::numeric_limits<int>::max();
    for (int mode = 0; mode <= 8; ++mode) {
        uint8_t pred[16];
        intra_4x4_predict(above, left, pred, mode);
        int s = sad_4x4(block, stride, pred);
        if (s < best_sad) {
            best_sad = s;
            best_mode = mode;
        }
    }
    return best_mode;
}

// ============================================================
// Golomb Reader (decode side)
// ============================================================
class GolombReader {
    const uint8_t* data_;
    size_t size_;
    size_t byte_pos_ = 0;
    int bit_pos_ = 0;

    int read_bit() {
        if (byte_pos_ >= size_) return 0;
        int bit = (data_[byte_pos_] >> (7 - bit_pos_)) & 1;
        ++bit_pos_;
        if (bit_pos_ == 8) { bit_pos_ = 0; ++byte_pos_; }
        return bit;
    }

public:
    GolombReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    uint32_t read_bits(int n) {
        uint32_t val = 0;
        for (int i = 0; i < n; ++i) val = (val << 1) | static_cast<uint32_t>(read_bit());
        return val;
    }

    unsigned ue() {
        int lead = 0;
        while (read_bit() == 0) {
            ++lead;
            if (lead > 31) break;
        }
        if (lead == 0) return 0;
        uint32_t suffix = read_bits(lead);
        return ((static_cast<unsigned>(1) << lead) | suffix) - 1;
    }

    int se() {
        unsigned v = ue();
        if (v & 1) return static_cast<int>((v + 1) >> 1);
        return -static_cast<int>(v >> 1);
    }

    int read_flag() { return read_bit(); }

    void skip_rbsp_trailing_bits() {
        // Read until we see a 1 bit, then skip zeros to byte align
        while (byte_pos_ < size_) {
            if (read_bit()) break;
        }
        // Skip zeros to byte align
        if (bit_pos_ != 0) { bit_pos_ = 0; ++byte_pos_; }
    }

    bool more_data() const { return byte_pos_ < size_; }

    size_t byte_offset() const { return byte_pos_; }
};

// ============================================================
// Remove emulation prevention bytes from RBSP
// ============================================================
std::vector<uint8_t> remove_epb(const uint8_t* data, size_t len) {
    std::vector<uint8_t> out;
    out.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        if (i >= 2 && out.size() >= 2 && out[out.size()-2] == 0 && out[out.size()-1] == 0 && data[i] == 3) {
            continue;
        }
        out.push_back(data[i]);
    }
    return out;
}

// ============================================================
// Extract NAL units from Annex B bitstream
// ============================================================
struct NalUnit {
    int nal_ref_idc;
    int nal_unit_type;
    std::vector<uint8_t> rbsp;
};

std::vector<NalUnit> extract_nal_units(const uint8_t* data, size_t len) {
    std::vector<NalUnit> nalus;
    size_t i = 0;
    while (i + 4 <= len) {
        // Find start code 0x00 0x00 0x01 (or 0x00 0x00 0x00 0x01)
        if (data[i] == 0 && data[i+1] == 0) {
            if (i+2 < len && data[i+2] == 1) {
                size_t start = i + 3;
                i = start;
                // Find end of this NAL
                size_t end = start;
                while (end < len) {
                    if (end + 2 < len && data[end] == 0 && data[end+1] == 0 && (data[end+2] == 1 || (end+3 < len && data[end+2] == 0 && data[end+3] == 1)))
                        break;
                    ++end;
                }
                size_t nal_len = end - start;
                if (nal_len > 0) {
                    // Parse NAL header
                    GolombReader nal_reader(data + start, nal_len);
                    nal_reader.read_flag(); // forbidden_zero_bit
                    int ref_idc = static_cast<int>(nal_reader.read_bits(2));
                    int type = static_cast<int>(nal_reader.read_bits(5));
                    size_t header_bytes = nal_reader.byte_offset();
                    // RBSP is after NAL header, with EPB removed
                    auto rbsp = remove_epb(data + start + header_bytes, nal_len - header_bytes);
                    nalus.push_back({ref_idc, type, std::move(rbsp)});
                }
                i = end;
                continue;
            } else if (i+3 < len && data[i+2] == 0 && data[i+3] == 1) {
                size_t start = i + 4;
                i = start;
                size_t end = start;
                while (end < len) {
                    if (end + 2 < len && data[end] == 0 && data[end+1] == 0 && (data[end+2] == 1 || (end+3 < len && data[end+2] == 0 && data[end+3] == 1)))
                        break;
                    ++end;
                }
                size_t nal_len = end - start;
                if (nal_len > 0) {
                    GolombReader nal_reader(data + start, nal_len);
                    nal_reader.read_flag();
                    int ref_idc = static_cast<int>(nal_reader.read_bits(2));
                    int type = static_cast<int>(nal_reader.read_bits(5));
                    size_t header_bytes = nal_reader.byte_offset();
                    auto rbsp = remove_epb(data + start + header_bytes, nal_len - header_bytes);
                    nalus.push_back({ref_idc, type, std::move(rbsp)});
                }
                i = end;
                continue;
            }
        }
        ++i;
    }
    return nalus;
}

// ============================================================
// SPS parsing
// ============================================================
struct SPSInfo {
    int width = 0;
    int height = 0;
    int log2_max_frame_num = 8;
};

SPSInfo parse_sps_rbsp(const uint8_t* data, size_t len) {
    SPSInfo sps;
    GolombReader r(data, len);
    r.read_bits(8);  // profile_idc
    r.read_flag();   // constraint_set0_flag
    r.read_flag();   // constraint_set1_flag
    r.read_flag();   // constraint_set2_flag
    r.read_flag();   // constraint_set3_flag
    r.read_bits(4);  // reserved
    r.read_bits(8);  // level_idc
    r.ue();           // seq_parameter_set_id
    sps.log2_max_frame_num = static_cast<int>(r.ue()) + 4;
    int poc_type = static_cast<int>(r.ue());
    if (poc_type == 0) {
        r.ue(); // log2_max_pic_order_cnt_lsb_minus4
    } else if (poc_type == 1) {
        r.read_flag(); // delta_pic_order_always_zero_flag
        r.se();        // offset_for_non_ref_pic
        r.se();        // offset_for_top_to_bottom_field
        int num_ref = static_cast<int>(r.ue());
        for (int i = 0; i < num_ref; ++i) r.se(); // offset_for_ref_frame
    }
    r.ue();           // max_num_ref_frames
    r.read_flag();    // gaps_in_frame_num_value_allowed_flag
    int mb_width = static_cast<int>(r.ue()) + 1;
    int mb_height = static_cast<int>(r.ue()) + 1;
    sps.width = mb_width * 16;
    sps.height = mb_height * 16;
    r.read_flag();    // frame_mbs_only_flag
    if (!r.read_flag()) { // frame_cropping_flag
        // no cropping
    }
    return sps;
}

// ============================================================
// PPS parsing
// ============================================================
struct PPSInfo {
    int qp = 26;
    int entropy_coding_mode = 0;
};

PPSInfo parse_pps_rbsp(const uint8_t* data, size_t len) {
    PPSInfo pps;
    GolombReader r(data, len);
    r.ue();           // pic_parameter_set_id
    r.ue();           // seq_parameter_set_id
    pps.entropy_coding_mode = r.read_flag();
    r.read_flag();    // bottom_field_pic_order_in_frame_present_flag
    r.ue();           // num_slice_groups_minus1
    r.ue();           // num_ref_idx_l0_active_minus1
    r.ue();           // num_ref_idx_l1_active_minus1
    r.read_flag();    // weighted_pred_flag
    r.read_bits(2);   // weighted_bipred_idc
    int qp_minus26 = r.se();
    pps.qp = 26 + qp_minus26;
    r.se();           // pic_init_qs_minus26
    r.se();           // chroma_qp_index_offset
    r.read_flag();    // deblocking_filter_control_present_flag
    r.read_flag();    // constrained_intra_pred_flag
    r.read_flag();    // redundant_pic_cnt_present_flag
    return pps;
}

// ============================================================
// Inverse 4x4 DCT (H.264 spec, Eq. 8-5)
// ============================================================
void idct_4x4(int16_t coeff[16]) {
    for (int i = 0; i < 4; ++i) {
        int a = coeff[i*4+0] + coeff[i*4+2];
        int b = coeff[i*4+0] - coeff[i*4+2];
        int c = (coeff[i*4+1] >> 1) - coeff[i*4+3];
        int d = coeff[i*4+1] + (coeff[i*4+3] >> 1);
        coeff[i*4+0] = static_cast<int16_t>(a + d);
        coeff[i*4+1] = static_cast<int16_t>(b + c);
        coeff[i*4+2] = static_cast<int16_t>(b - c);
        coeff[i*4+3] = static_cast<int16_t>(a - d);
    }
    for (int j = 0; j < 4; ++j) {
        int a = coeff[0*4+j] + coeff[2*4+j];
        int b = coeff[0*4+j] - coeff[2*4+j];
        int c = (coeff[1*4+j] >> 1) - coeff[3*4+j];
        int d = coeff[1*4+j] + (coeff[3*4+j] >> 1);
        coeff[0*4+j] = static_cast<int16_t>((a + d + 32) >> 6);
        coeff[1*4+j] = static_cast<int16_t>((b + c + 32) >> 6);
        coeff[2*4+j] = static_cast<int16_t>((b - c + 32) >> 6);
        coeff[3*4+j] = static_cast<int16_t>((a - d + 32) >> 6);
    }
}

// ============================================================
// Inverse Quantization (H.264 spec, Eq. 8-4)
// V tables for 4x4 (position index 0=DC, 1=cross, 2=diagonal)
// ============================================================
const int V_4x4[6][3] = {
    {10, 16, 13},
    {11, 18, 14},
    {13, 20, 16},
    {14, 23, 18},
    {16, 25, 20},
    {18, 29, 23}
};

void dequant_4x4(int16_t coeff[16], int qp) {
    int mf_idx = qp % 6;
    int shift = qp / 6;
    for (int i = 0; i < 16; ++i) {
        if (coeff[i] == 0) continue;
        int x = i % 4, y = i / 4;
        int pos_table;
        if (x == 0 && y == 0) pos_table = 0;
        else if ((x + y) % 2 == 0) pos_table = 2;
        else pos_table = 1;
        int level = std::abs(coeff[i]);
        int val = level * V_4x4[mf_idx][pos_table];
        val <<= shift;
        if (coeff[i] < 0) val = -val;
        coeff[i] = static_cast<int16_t>(std::clamp(val, -32768, 32767));
    }
}

// ============================================================
// Decode residual block (reverse of encode_residual_block)
// ============================================================
void decode_residual_block(GolombReader& r, int16_t coeff[16]) {
    for (int i = 0; i < 16; ++i) coeff[i] = 0;

    if (!r.more_data()) return;

    unsigned num_coeff = r.ue();

    if (num_coeff == 0 || num_coeff > 16) return;

    unsigned ti = r.ue();
    if (ti > 3) ti = 3;
    if (ti > num_coeff) ti = num_coeff;

    uint8_t ti_signs = 0;
    for (unsigned i = 0; i < ti; ++i) {
        ti_signs = static_cast<uint8_t>((ti_signs << 1) | static_cast<uint8_t>(r.read_flag()));
    }

    int16_t levels[16];
    unsigned num_nt = num_coeff - ti;
    for (unsigned i = 0; i < num_nt; ++i) {
        levels[i] = static_cast<int16_t>(r.se());
    }

    unsigned total_zeros = r.ue();
    if (total_zeros > 16) total_zeros = 0;

    unsigned runs[16];
    unsigned run_count = 0;
    unsigned zeros_left = total_zeros;

    for (unsigned i = 0; i < num_nt && zeros_left > 0; ++i) {
        unsigned run = r.ue();
        runs[i] = run;
        zeros_left -= (run > zeros_left ? zeros_left : run);
        ++run_count;
    }
    for (unsigned i = run_count; i < num_nt; ++i) runs[i] = 0;

    int16_t scanned[16];
    for (int i = 0; i < 16; ++i) scanned[i] = 0;

    int pos = 15;
    for (int t = static_cast<int>(ti) - 1; t >= 0; --t) {
        int8_t sign = (ti_signs & (1 << t)) ? -1 : 1;
        scanned[pos--] = sign;
    }

    if (num_nt > 0) {
        unsigned sum_runs = 0;
        for (unsigned i = 0; i < run_count; ++i) sum_runs += runs[i];
        unsigned zeros_before_first = total_zeros > sum_runs ? total_zeros - sum_runs : 0;

        pos = static_cast<int>(15 - ti);
        pos -= static_cast<int>(zeros_before_first);

        for (unsigned k = 0; k < num_nt; ++k) {
            if (pos < 0) break;
            scanned[pos] = levels[k];
            if (k < run_count) {
                pos -= static_cast<int>(runs[k]) + 1;
            }
        }
    }

    for (int i = 0; i < 16; ++i) {
        coeff[ZIGZAG[i]] = scanned[i];
    }
}

// ============================================================
// Decode IDR slice (reverse of encode_idr_slice)
// ============================================================
void decode_idr_slice(const uint8_t* rbsp_data, size_t rbsp_len,
                      const SPSInfo& sps, const PPSInfo& pps,
                      uint8_t* y_plane, uint8_t* u_plane, uint8_t* v_plane,
                      int width, int height) {
    GolombReader r(rbsp_data, rbsp_len);

    int mb_width = (width + 15) / 16;
    int mb_height = (height + 15) / 16;

    // ── Slice header ──
    /*first_mb_in_slice =*/ r.ue();
    /*slice_type =*/ r.ue();
    /*pic_parameter_set_id =*/ r.ue();
    int log2_fnum = sps.log2_max_frame_num;
    int frame_num_val = static_cast<int>(r.read_bits(log2_fnum));
    /*idr_pic_id =*/ r.ue();
    /*pic_order_cnt_lsb =*/ r.read_bits(8);
    /*no_output_of_prior_pics_flag =*/ r.read_flag();
    /*long_term_reference_flag =*/ r.read_flag();
    /*slice_qp_delta =*/ r.se();
    /*disable_deblocking_filter_idc =*/ r.ue();

    int mb_qp = pps.qp;

    // ── Macroblock layer ──
    for (int mb_y = 0; mb_y < mb_height; ++mb_y) {
        for (int mb_x = 0; mb_x < mb_width; ++mb_x) {
            // mb_type
            unsigned mb_type = r.ue();

            if (mb_type == 0) {
                // I_4x4: skip intra prediction mode flags (encoder writes 16 flags)
                for (int i = 0; i < 16; ++i) r.read_flag();
            } else if (mb_type <= 3 && mb_type < 2) {
                // I_16x16 with pred mode
                r.ue();
            }
            // mb_type >= 2: I_16x16 DC, no extra pred mode needed

            // chroma_pred_mode
            r.ue();

            // cbp (coded block pattern)
            unsigned cbp = r.ue();
            unsigned luma_cbp = cbp & 0x0F;
            unsigned chroma_cbp = (cbp >> 4) & 0x03;

            // mb_qp_delta
            /*mb_qp_delta =*/ r.se();

            int cur_qp = mb_qp;
            int chroma_qp = std::clamp(cur_qp + 3, 0, 51);

            // ── Luma residual ──
            for (int by = 0; by < 4; ++by) {
                for (int bx = 0; bx < 4; ++bx) {
                    int sub_idx = by * 4 + bx;

                    int16_t coeff[16];
                    decode_residual_block(r, coeff);

                    // Check if any coeffs are non-zero
                    bool has_coeff = false;
                    for (int i = 0; i < 16; ++i) {
                        if (coeff[i] != 0) { has_coeff = true; break; }
                    }

                    if (has_coeff) {
                        dequant_4x4(coeff, cur_qp);
                        idct_4x4(coeff);
                    } else {
                        for (int i = 0; i < 16; ++i) coeff[i] = 0;
                    }

                    // Reconstruct: add DC prediction (128) and clip
                    int ox = mb_x * 16 + bx * 4;
                    int oy = mb_y * 16 + by * 4;
                    for (int py = 0; py < 4; ++py) {
                        for (int px = 0; px < 4; ++px) {
                            int sx = ox + px;
                            int sy = oy + py;
                            if (sx < width && sy < height) {
                                int recon = coeff[py * 4 + px] + 128;
                                y_plane[sy * width + sx] = static_cast<uint8_t>(std::clamp(recon, 0, 255));
                            }
                        }
                    }
                }
            }

            // ── Chroma residual ──
            bool chroma_coded = (chroma_cbp != 0);

            for (int ch = 0; ch < 2; ++ch) {
                uint8_t* plane = (ch == 0) ? u_plane : v_plane;
                int uv_width = width / 2;
                int uv_height = height / 2;

                for (int by = 0; by < 2; ++by) {
                    for (int bx = 0; bx < 2; ++bx) {
                        int16_t coeff[16];
                        if (chroma_coded) {
                            decode_residual_block(r, coeff);
                        } else {
                            for (int i = 0; i < 16; ++i) coeff[i] = 0;
                        }

                        bool has_coeff = false;
                        for (int i = 0; i < 16; ++i) {
                            if (coeff[i] != 0) { has_coeff = true; break; }
                        }

                        if (has_coeff) {
                            dequant_4x4(coeff, chroma_qp);
                            idct_4x4(coeff);
                        } else {
                            for (int i = 0; i < 16; ++i) coeff[i] = 0;
                        }

                        int ox = mb_x * 8 + bx * 4;
                        int oy = mb_y * 8 + by * 4;
                        for (int py = 0; py < 4; ++py) {
                            for (int px = 0; px < 4; ++px) {
                                int sx = ox + px;
                                int sy = oy + py;
                                if (sx < uv_width && sy < uv_height) {
                                    int recon = coeff[py * 4 + px] + 128;
                                    plane[sy * uv_width + sx] = static_cast<uint8_t>(std::clamp(recon, 0, 255));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

// ============================================================
// YUV420 → RGB24 (BT.601)
// ============================================================
void yuv420_to_rgb24(const uint8_t* y, const uint8_t* u, const uint8_t* v,
                     int width, int height, uint8_t* rgb) {
    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            int Y = y[py * width + px];
            int U_idx = (py / 2) * (width / 2) + (px / 2);
            int V_idx = U_idx;
            int C = Y - 16;
            int D = u[U_idx] - 128;
            int E = v[V_idx] - 128;

            int R = (298 * C + 409 * E + 128) >> 8;
            int G = (298 * C - 100 * D - 208 * E + 128) >> 8;
            int B = (298 * C + 516 * D + 128) >> 8;

            size_t rgb_idx = static_cast<size_t>(py * width + px) * 3;
            rgb[rgb_idx + 0] = static_cast<uint8_t>(std::clamp(R, 0, 255));
            rgb[rgb_idx + 1] = static_cast<uint8_t>(std::clamp(G, 0, 255));
            rgb[rgb_idx + 2] = static_cast<uint8_t>(std::clamp(B, 0, 255));
        }
    }
}

}  // namespace

// ── RGB24 → YUV420 ──

void h264::rgb24_to_yuv420(const uint8_t* rgb, int width, int height,
                           std::vector<uint8_t>& y,
                           std::vector<uint8_t>& u,
                           std::vector<uint8_t>& v) {
    y.resize(static_cast<size_t>(width * height));
    u.resize(static_cast<size_t>((width / 2) * (height / 2)));
    v.resize(static_cast<size_t>((width / 2) * (height / 2)));

    for (int py = 0; py < height; ++py) {
        for (int px = 0; px < width; ++px) {
            size_t rgb_idx = static_cast<size_t>(py * width + px) * 3;
            int R = rgb[rgb_idx];
            int G = rgb[rgb_idx + 1];
            int B = rgb[rgb_idx + 2];

            // BT.601
            int Y_val = ((66 * R + 129 * G + 25 * B + 128) >> 8) + 16;
            y[static_cast<size_t>(py * width + px)] = static_cast<uint8_t>(std::clamp(Y_val, 0, 255));

            if (px % 2 == 0 && py % 2 == 0) {
                int U_val = ((-38 * R - 74 * G + 112 * B + 128) >> 8) + 128;
                int V_val = ((112 * R - 94 * G - 18 * B + 128) >> 8) + 128;
                size_t uv_idx = static_cast<size_t>((py / 2) * (width / 2) + (px / 2));
                u[uv_idx] = static_cast<uint8_t>(std::clamp(U_val, 0, 255));
                v[uv_idx] = static_cast<uint8_t>(std::clamp(V_val, 0, 255));
            }
        }
    }
}

// ── Generate SPS ──

auto h264::generate_sps(int width, int height, int qp) -> std::vector<uint8_t> {
    static_cast<void>(qp);
    GolombWriter w;

    // profile_idc: 66 = Baseline
    w.write_bits(66, 8);
    // constraint_set0_flag: 1 (Baseline)
    w.write_flag(1);
    w.write_flag(0); // constraint_set1_flag
    w.write_flag(0); // constraint_set2_flag
    w.write_flag(0); // constraint_set3_flag
    w.write_bits(0, 4); // reserved
    // level_idc: e.g., 13 = Level 1.3 (supports up to 176x144, but we'll use a reasonable level)
    int luma_pels = width * height;
    int level;
    if (luma_pels <= 25344) level = 10;
    else if (luma_pels <= 101376) level = 11;
    else if (luma_pels <= 405504) level = 20;
    else if (luma_pels <= 1048576) level = 30;
    else level = 40;
    w.write_bits(static_cast<uint32_t>(level), 8);

    // seq_parameter_set_id: ue(v)
    w.ue(0);

    // log2_max_frame_num_minus4
    w.ue(4); // max_frame_num = 2^(4+4) = 256

    // pic_order_cnt_type: 0
    w.ue(0);
    // For type 0: log2_max_pic_order_cnt_lsb_minus4
    w.ue(4);

    // max_num_ref_frames: ue(v) = 1
    w.ue(1);

    // gaps_in_frame_num_value_allowed_flag: 0
    w.write_flag(0);

    // pic_width_in_mbs_minus1
    w.ue(static_cast<unsigned>((width + 15) / 16 - 1));

    // pic_height_in_map_units_minus1
    w.ue(static_cast<unsigned>((height + 15) / 16 - 1));

    // frame_mbs_only_flag: 1 (progressive)
    w.write_flag(1);

    // direct_8x8_inference_flag: 0
    w.write_flag(0);

    // frame_cropping_flag: 0
    w.write_flag(0);

    // vui_parameters_present_flag: 0
    w.write_flag(0);

    w.rbsp_trailing_bits();
    std::vector<uint8_t> rbsp = w.take_buffer();
    return pack_nal(rbsp, 3, 7); // nal_ref_idc=3, type=SPS(7)
}

// ── Generate PPS ──

auto h264::generate_pps(int qp) -> std::vector<uint8_t> {
    GolombWriter w;

    // pic_parameter_set_id: ue(v) = 0
    w.ue(0);
    // seq_parameter_set_id: ue(v) = 0
    w.ue(0);

    // entropy_coding_mode_flag: 0 (CAVLC)
    w.write_flag(0);

    // pic_order_present_flag: 0
    w.write_flag(0);

    // num_slice_groups_minus1: 0
    w.ue(0);

    // num_ref_idx_l0_active_minus1: 0
    w.ue(0);

    // num_ref_idx_l1_active_minus1: 0
    w.ue(0);

    // weighted_pred_flag: 0
    w.write_flag(0);
    // weighted_bipred_idc: 0
    w.write_bits(0, 2);

    // pic_init_qp_minus26: se(v) = qp - 26
    w.se(qp - 26);

    // pic_init_qs_minus26: se(v) = 0
    w.se(0);

    // chroma_qp_index_offset: se(v) = 0
    w.se(0);

    // deblocking_filter_control_present_flag: 0
    w.write_flag(0);

    // constrained_intra_pred_flag: 0
    w.write_flag(0);

    // redundant_pic_cnt_present_flag: 0
    w.write_flag(0);

    // transform_8x8_mode_flag: 0 (requires high profile)
    w.rbsp_trailing_bits();

    std::vector<uint8_t> rbsp = w.take_buffer();
    return pack_nal(rbsp, 3, 8); // nal_ref_idc=3, type=PPS(8)
}

// ── Encode IDR slice ──

auto h264::encode_idr_slice(const uint8_t* y_plane, const uint8_t* u_plane,
                            const uint8_t* v_plane,
                            int width, int height, int qp,
                            int frame_num, int idr_pic_id) -> std::vector<uint8_t> {
    int mb_width = (width + 15) / 16;
    int mb_height = (height + 15) / 16;
    int mb_count = mb_width * mb_height;

    GolombWriter w;

    // ── Slice header ──
    // first_mb_in_slice: ue(v) = 0
    w.ue(0);
    // slice_type: ue(v) = 2 (I slice)
    w.ue(2);
    // pic_parameter_set_id: ue(v) = 0
    w.ue(0);
    // frame_num (for IDR, this is 0 if no gaps)
    w.write_bits(static_cast<uint32_t>(frame_num), 8); // log2_max_frame_num_minus4 = 4 → 8 bits

    // IdrPicId: ue(v) — always present for IDR slices (H.264 spec 7.3.3)
    w.ue(static_cast<unsigned>(idr_pic_id));

    // pic_order_cnt_lsb: 8 bits
    w.write_bits(0, 8);

    // no_output_of_prior_pics_flag: 0
    w.write_flag(0);
    // long_term_reference_flag: 0
    w.write_flag(0);

    // slice_qp_delta: se(v) = 0
    w.se(0);

    // disable_deblocking_filter_idc: 0
    w.ue(0);

    // ── Macroblock layer ──
    for (int mb_y = 0; mb_y < mb_height; ++mb_y) {
        for (int mb_x = 0; mb_x < mb_width; ++mb_x) {
            // mb_type for I_4x4 (0) ... I_16x16 (25)
            // Use I_16x16 (DC prediction) for simplicity
            int mb_type = 0; // I_NxN = 0, Intra 4x4

            // Check if we should use I_16x16 DC (type 2)
            // For simplicity, always use I_4x4 (mb_type = 0)
            // Actually let's use I_16x16 DC = mb_type 2 for simpler encoding
            mb_type = 2; // I_16x16 DC prediction

            w.ue(static_cast<unsigned>(mb_type));

            if (mb_type == 0) {
                // I_4x4: encode intra prediction modes for each 4x4 subblock
                for (int by = 0; by < 4; ++by) {
                    for (int bx = 0; bx < 4; ++bx) {
                        // Use DC mode (2) for all subblocks
                        w.write_flag(1); // prev_intra4x4_pred_mode_flag = 1
                    }
                }
            } else if (mb_type <= 3) {
                // I_16x16: encode intra16x16_pred_mode
                // mb_type 2 = I_16x16 DC (no need for extra pred mode)
                // For mb_type 0/1, need to signal pred mode
                if (mb_type < 2) {
                    // Actually I_16x16 modes: 0=Vertical,1=Horizontal,2=DC,3=Plane
                    // mb_type 0=PredL0, 1=Pred
                    // For simplicity, if mb_type is 0 or 1, signal pred mode
                    if (mb_type == 0) {
                        w.ue(2); // DC prediction for I_16x16
                    } else {
                        w.ue(2); // DC
                    }
                }
            }

            // chroma_pred_mode: ue(v) = 0 (DC)
            w.ue(0);

            // cbp (coded block pattern)
            // For I_16x16: CBP = luma_cbp (0 or 1) + chroma_cbp (0-2)
            // luma_cbp = 1 if any 4x4 block has non-zero coeffs
            // chroma_cbp = 0, 1, or 2
            // We'll compute actual CBP from the encoded data
            // For simplicity, assume all blocks are coded
            int cbp = 15 + 2 * 16; // luma=15 (all 4 subblocks), chroma=2 (both)
            w.ue(static_cast<unsigned>(cbp));

            // QP' delta = 0
            w.se(0);

            // ── Luma residual (16x16) ──
            // For I_16x16, each of 16 4x4 subblocks
            for (int by = 0; by < 4; ++by) {
                for (int bx = 0; bx < 4; ++bx) {
                    // Extract 4x4 block from Y plane
                    int ox = mb_x * 16 + bx * 4;
                    int oy = mb_y * 16 + by * 4;
                    int16_t block[16];
                    for (int py = 0; py < 4; ++py)
                        for (int px = 0; px < 4; ++px) {
                            int sx = std::min(ox + px, width - 1);
                            int sy = std::min(oy + py, height - 1);
                            block[py * 4 + px] = static_cast<int16_t>(y_plane[sy * width + sx]);
                        }

                    // DC prediction: subtract 128
                    for (int i = 0; i < 16; ++i)
                        block[i] = static_cast<int16_t>(static_cast<int>(block[i]) - 128);

                    // DCT + Quant
                    int16_t coeff[16];
                    dct_4x4(block, coeff);
                    int nnz = quant_4x4(coeff, qp);

                    if (nnz == 0) {
                        // If CBP says coded but block has no coeffs, still need to encode something
                        // For simplicity, encode one zero-level coeff
                        encode_residual_block(w, coeff);
                    } else {
                        encode_residual_block(w, coeff);
                    }
                }
            }

            // ── Chroma residual (8x8 Cb, Cr) ──
            for (int ch = 0; ch < 2; ++ch) {
                const uint8_t* plane = (ch == 0) ? u_plane : v_plane;
                int uv_width = width / 2;
                int uv_height = height / 2;
                int uv_mb_x = mb_x;
                int uv_mb_y = mb_y;

                for (int by = 0; by < 2; ++by) {
                    for (int bx = 0; bx < 2; ++bx) {
                        int ox = uv_mb_x * 8 + bx * 4;
                        int oy = uv_mb_y * 8 + by * 4;
                        int16_t block[16];
                        for (int py = 0; py < 4; ++py)
                            for (int px = 0; px < 4; ++px) {
                                int sx = std::min(ox + px, uv_width - 1);
                                int sy = std::min(oy + py, uv_height - 1);
                                block[py * 4 + px] = static_cast<int16_t>(plane[sy * uv_width + sx]);
                            }

                        for (int i = 0; i < 16; ++i)
                            block[i] = static_cast<int16_t>(static_cast<int>(block[i]) - 128);

                        int16_t coeff[16];
                        dct_4x4(block, coeff);
                        int nnz = quant_4x4(coeff, qp + 3); // chroma QP offset

                        encode_residual_block(w, coeff);
                    }
                }
            }
        }
    }

    // End of slice: rbsp_trailing_bits
    w.rbsp_trailing_bits();

    std::vector<uint8_t> rbsp = w.take_buffer();
    return pack_nal(rbsp, 3, 5); // nal_ref_idc=3, type=IDR(5)
}

// ── Public API ──

auto video_compress(const std::vector<uint8_t>& raw_data,
                    VideoFormat format, int quality) -> std::vector<uint8_t> {
    static_cast<void>(format);

    auto info = parse_raw_video_header(raw_data);
    if (info.width == 0 || info.height == 0 || info.num_frames == 0)
        return {};

    int w = info.width;
    int h = info.height;
    int qp = std::clamp(quality, 0, 51);

    // Generate SPS and PPS
    std::vector<uint8_t> out;
    auto sps = h264::generate_sps(w, h, qp);
    auto pps = h264::generate_pps(qp);
    out.insert(out.end(), sps.begin(), sps.end());
    out.insert(out.end(), pps.begin(), pps.end());

    const uint8_t* frame_ptr = raw_data.data() + 16;
    size_t frame_size = static_cast<size_t>(w * h * 3); // RGB24

    for (int fnum = 0; fnum < info.num_frames; ++fnum) {
        const uint8_t* rgb_frame = frame_ptr + fnum * frame_size;

        // Convert RGB to YUV420
        std::vector<uint8_t> y, u, v;
        h264::rgb24_to_yuv420(rgb_frame, w, h, y, u, v);

        // Encode as IDR slice
        auto slice = h264::encode_idr_slice(y.data(), u.data(), v.data(),
                                             w, h, qp, fnum, fnum);
        out.insert(out.end(), slice.begin(), slice.end());
    }

    return out;
}

auto video_decompress(const std::vector<uint8_t>& h264_data) -> std::vector<uint8_t> {
    if (h264_data.size() < 16) return {};

    auto nalus = extract_nal_units(h264_data.data(), h264_data.size());
    if (nalus.empty()) return {};

    SPSInfo sps;
    PPSInfo pps;
    bool have_sps = false, have_pps = false;

    int num_frames = 0;
    for (auto& nalu : nalus) {
        if (nalu.nal_unit_type == 5) { num_frames++; }
    }

    for (auto& nalu : nalus) {
        if (nalu.nal_unit_type == 7 && !have_sps) {
            sps = parse_sps_rbsp(nalu.rbsp.data(), nalu.rbsp.size());
            have_sps = true;
        } else if (nalu.nal_unit_type == 8 && !have_pps) {
            pps = parse_pps_rbsp(nalu.rbsp.data(), nalu.rbsp.size());
            have_pps = true;
        }
    }

    if (!have_sps || !have_pps) return {};

    int w = sps.width;
    int h = sps.height;
    size_t frame_rgb_size = static_cast<size_t>(w * h * 3);

    std::vector<uint8_t> all_frames;
    all_frames.reserve(static_cast<size_t>(num_frames) * frame_rgb_size);

    int frame_count = 0;
    for (auto& nalu : nalus) {
        if (nalu.nal_unit_type != 5) continue;

        std::vector<uint8_t> y(static_cast<size_t>(w * h));
        std::vector<uint8_t> u(static_cast<size_t>((w / 2) * (h / 2)));
        std::vector<uint8_t> v(static_cast<size_t>((w / 2) * (h / 2)));

        decode_idr_slice(nalu.rbsp.data(), nalu.rbsp.size(),
                         sps, pps, y.data(), u.data(), v.data(), w, h);

        std::vector<uint8_t> rgb(frame_rgb_size);
        yuv420_to_rgb24(y.data(), u.data(), v.data(), w, h, rgb.data());

        all_frames.insert(all_frames.end(), rgb.begin(), rgb.end());
        ++frame_count;
    }

    return build_raw_video(all_frames, w, h, 30, frame_count, 0);
}

}  // namespace compressor::algorithm
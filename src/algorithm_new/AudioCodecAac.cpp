#include "AudioCodec.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <vector>

namespace compressor::algorithm::aac {

// ── Constants ──

constexpr int BLOCK_LEN_LONG = 2048;
constexpr int BLOCK_LEN_SHORT = 256;
constexpr int NUM_SHORT_WINDOWS = 8;
constexpr int SPECTRAL_LINES_LONG = 1024;
constexpr int SPECTRAL_LINES_SHORT = 128;
constexpr int FRAME_SIZE = 1024;

constexpr int get_sampling_index(int rate) {
    switch (rate) {
        case 96000: return 0;
        case 88200: return 1;
        case 64000: return 2;
        case 48000: return 3;
        case 44100: return 4;
        case 32000: return 5;
        case 24000: return 6;
        case 22050: return 7;
        case 16000: return 8;
        case 12000: return 9;
        case 11025: return 10;
        case 8000:  return 11;
        default:    return 4;
    }
}

static int sampling_index_to_rate(int idx) {
    switch (idx) {
        case 0:  return 96000;
        case 1:  return 88200;
        case 2:  return 64000;
        case 3:  return 48000;
        case 4:  return 44100;
        case 5:  return 32000;
        case 6:  return 24000;
        case 7:  return 22050;
        case 8:  return 16000;
        case 9:  return 12000;
        case 10: return 11025;
        case 11: return 8000;
        default: return 44100;
    }
}

constexpr int NUM_SFB_LONG_44K = 49;
constexpr int NUM_SFB_SHORT_44K = 14;

static const int SFB_OFFSET_LONG_44K[NUM_SFB_LONG_44K + 1] = {
    0, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 48, 56, 64, 72, 80, 88,
    96, 108, 120, 132, 144, 160, 176, 196, 216, 240, 264, 292, 320, 352,
    384, 416, 448, 480, 512, 544, 576, 608, 640, 672, 704, 736, 768, 800,
    832, 864, 896, 928, 1024
};

static const int SFB_OFFSET_SHORT_44K[NUM_SFB_SHORT_44K + 1] = {
    0, 4, 8, 12, 16, 20, 28, 36, 44, 56, 68, 80, 96, 112, 128
};

static const double ABS_THRESH[32] = {
    60.0, 55.0, 50.0, 45.0, 40.0, 35.0, 30.0, 25.0,
    22.0, 19.0, 16.0, 14.0, 12.0, 10.0, 8.5,  7.5,
    6.5,  5.5,  4.5,  3.5,  3.0,  2.5,  2.0,  1.5,
    1.2,  1.0,  0.8,  0.6,  0.5,  0.4,  0.3,  0.2
};

// ── Sine window ──

static void sine_window(const double* input, double* output, int n) {
    for (int i = 0; i < n; ++i) {
        double w = std::sin(M_PI * (static_cast<double>(i) + 0.5) / static_cast<double>(n));
        output[i] = input[i] * w;
    }
}

// ── Forward MDCT ──

static void mdct(const double* x, double* X, int N) {
    int M = N / 2;
    double norm = 1.0;
    for (int k = 0; k < M; ++k) {
        double sum = 0.0;
        for (int n = 0; n < N; ++n) {
            double arg = 2.0 * M_PI / static_cast<double>(N) *
                         (static_cast<double>(n) + static_cast<double>(N) / 4.0 + 0.5) *
                         (static_cast<double>(k) + 0.5);
            sum += x[n] * std::cos(arg);
        }
        X[k] = sum * norm;
    }
}

// ── Inverse MDCT ──
// ISO AAC standard: forward=1, inverse=4/N, total=4/N for TDAC

static void imdct(const double* X, double* y, int N) {
    int M = N / 2;
    double scale = 4.0 / static_cast<double>(N);
    for (int n = 0; n < N; ++n) {
        double sum = 0.0;
        for (int k = 0; k < M; ++k) {
            double arg = 2.0 * M_PI / static_cast<double>(N) *
                         (static_cast<double>(n) + static_cast<double>(N) / 4.0 + 0.5) *
                         (static_cast<double>(k) + 0.5);
            sum += X[k] * std::cos(arg);
        }
        y[n] = sum * scale;
    }
}

// ── Block switching via simple transient detection ──

static bool detect_transient(const double* frame, int frame_len, int sub_blocks) {
    int sub_len = frame_len / sub_blocks;
    double prev_energy = 0.0;
    for (int s = 0; s < sub_blocks; ++s) {
        double e = 0.0;
        for (int i = 0; i < sub_len; ++i)
            e += frame[s * sub_len + i] * frame[s * sub_len + i];
        if (s > 0 && e > prev_energy * 8.0 && e > 1e6)
            return true;
        prev_energy = std::max(e, 1.0);
    }
    return false;
}

// ── Simplified psychoacoustic model ──

static void compute_masking_threshold(const double* spectrum, int num_lines,
                                       const int* sfb_offset, int num_sfb,
                                       int sample_rate, double* threshold) {
    double bark_width = (static_cast<double>(sample_rate) / 2.0) / (num_lines + 1);
    double prev_thr = std::numeric_limits<double>::max();

    for (int sfb = 0; sfb < num_sfb; ++sfb) {
        int start = sfb_offset[sfb];
        int end = sfb_offset[sfb + 1];
        if (end > num_lines) end = num_lines;
        if (start >= end) { threshold[sfb] = prev_thr; continue; }

        double band_energy = 0.0;
        for (int i = start; i < end; ++i)
            band_energy += spectrum[i] * spectrum[i];
        band_energy = std::max(band_energy, 1e-12);

        double center_freq = bark_width * static_cast<double>(start + end) / 2.0;
        int bark_idx = static_cast<int>(center_freq * 0.172);

        double abs_thr = 100.0;
        if (bark_idx >= 0 && bark_idx < 32)
            abs_thr = std::pow(10.0, ABS_THRESH[bark_idx] / 10.0);

        double spread_factor = (sfb > 0) ? 0.25 : 1.0;
        double tonality = 0.5;
        double masking = band_energy * spread_factor * (1.0 - tonality * 0.7);

        threshold[sfb] = std::max(masking, abs_thr);
        prev_thr = threshold[sfb];
    }
}

// ── Quantization ──

static int quantize(double mdct_line, double scale) {
    double mag = std::abs(mdct_line) * scale;
    double q = std::pow(mag, 3.0 / 4.0);
    int q_int = static_cast<int>(q + 0.5);
    return (mdct_line >= 0) ? q_int : -q_int;
}

static double dequantize(int quant_val, double inv_scale) {
    if (quant_val == 0) return 0.0;
    double mag = std::pow(std::abs(static_cast<double>(quant_val)), 4.0 / 3.0);
    return (quant_val > 0 ? mag : -mag) * inv_scale;
}

static double estimate_scale(int bitrate_kbps, int sample_rate, int num_channels) {
    (void)sample_rate;
    double base = 0.4;
    double bitrate_factor = static_cast<double>(bitrate_kbps) / 128.0;
    double ch_factor = std::sqrt(static_cast<double>(num_channels));
    return base * bitrate_factor / ch_factor;
}

// Convert quantizer scale to 8-bit global_gain for bitstream
static int scale_to_global_gain(double scale) {
    int gg = static_cast<int>(-std::log2(std::max(scale, 1e-12)) * 16.0 + 128.0);
    return std::max(0, std::min(255, gg));
}

// Convert 8-bit global_gain back to quantizer scale
static double global_gain_to_scale(int global_gain) {
    return std::pow(2.0, (128.0 - static_cast<double>(global_gain)) / 16.0);
}

// ── AAC Bit Writer (MSB-first) ──

class AacBitWriter {
    std::vector<uint8_t> buf_;
    int bit_pos_ = 0;

public:
    void write_bit(int bit) {
        if (bit_pos_ == 0)
            buf_.push_back(0);
        if (bit)
            buf_.back() |= static_cast<uint8_t>(1 << (7 - bit_pos_));
        bit_pos_ = (bit_pos_ + 1) & 7;
    }

    void write_bits(uint32_t val, int nbits) {
        for (int i = nbits - 1; i >= 0; --i)
            write_bit((val >> i) & 1);
    }

    void flush() {
        if (bit_pos_ != 0)
            bit_pos_ = 0;
    }

    void write_byte(uint8_t b) {
        if (bit_pos_ == 0)
            buf_.push_back(b);
        else
            write_bits(b, 8);
    }

    size_t size() const { return buf_.size(); }
    const std::vector<uint8_t>& buffer() const { return buf_; }
    std::vector<uint8_t> take_buffer() { flush(); return std::move(buf_); }
};

// ── AAC Bit Reader (MSB-first) ──

class AacBitReader {
    const uint8_t* buf_;
    size_t size_;
    size_t byte_pos_;
    int bit_pos_;

public:
    AacBitReader(const uint8_t* buf, size_t size)
        : buf_(buf), size_(size), byte_pos_(0), bit_pos_(0) {}

    int read_bit() {
        if (byte_pos_ >= size_) return 0;
        int bit = (buf_[byte_pos_] >> (7 - bit_pos_)) & 1;
        bit_pos_++;
        if (bit_pos_ >= 8) {
            bit_pos_ = 0;
            byte_pos_++;
        }
        return bit;
    }

    uint32_t read_bits(int nbits) {
        uint32_t val = 0;
        for (int i = 0; i < nbits; ++i)
            val = (val << 1) | static_cast<uint32_t>(read_bit());
        return val;
    }

    void byte_align() {
        if (bit_pos_ != 0) {
            bit_pos_ = 0;
            byte_pos_++;
        }
    }

    size_t bits_left() const {
        if (byte_pos_ >= size_) return 0;
        return (size_ - byte_pos_) * 8 - static_cast<size_t>(bit_pos_);
    }

    bool eof() const { return byte_pos_ >= size_; }

    size_t byte_pos() const { return byte_pos_; }
};

// ── Helper: bit count needed for a value ──

static int bits_for_value(int max_val) {
    if (max_val <= 0) return 0;
    int bits = 0;
    while (max_val > 0) { bits++; max_val >>= 1; }
    return bits;
}

// ── Simplified spectral data writer (unambiguous bit-packing) ──

static void write_spectral_data_simple(AacBitWriter& w, const int* quant_spectral,
                                        int num_lines, const int* sfb_offset,
                                        int num_sfb, int num_windows) {
    for (int win = 0; win < num_windows; ++win) {
        for (int sfb = 0; sfb < num_sfb; ++sfb) {
            int start = sfb_offset[sfb];
            int end = sfb_offset[sfb + 1];
            if (end > num_lines) end = num_lines;

            bool has_data = false;
            for (int i = start; i < end; ++i) {
                if (quant_spectral[win * num_lines + i] != 0) {
                    has_data = true;
                    break;
                }
            }

            w.write_bit(has_data ? 1 : 0);
            if (!has_data) continue;

            for (int idx = start; idx < end; idx += 2) {
                int v1 = (idx < num_lines) ? quant_spectral[win * num_lines + idx] : 0;
                int v2 = (idx + 1 < num_lines) ? quant_spectral[win * num_lines + idx + 1] : 0;

                int v1_abs = std::abs(v1);
                int v2_abs = std::abs(v2);

                bool is_nonzero = (v1_abs != 0 || v2_abs != 0);
                w.write_bit(is_nonzero ? 1 : 0);
                if (!is_nonzero) continue;

                int max_abs = std::max(v1_abs, v2_abs);
                int bp = bits_for_value(max_abs);
                if (bp == 0) bp = 1;
                int bp_enc = bp - 1;
                if (bp_enc > 15) bp_enc = 15;
                w.write_bits(static_cast<uint32_t>(bp_enc), 4);

                uint32_t mask = (1u << bp) - 1;
                w.write_bits(static_cast<uint32_t>(v1_abs) & mask, bp);
                w.write_bits(static_cast<uint32_t>(v2_abs) & mask, bp);

                if (v1_abs > 0) w.write_bit(v1 > 0 ? 1 : 0);
                if (v2_abs > 0) w.write_bit(v2 > 0 ? 1 : 0);
            }
        }
    }
}

// ── Simplified spectral data reader ──

static void read_spectral_data_simple(AacBitReader& r, int* quant_spectral,
                                       int num_lines, const int* sfb_offset,
                                       int num_sfb, int num_windows) {
    for (int win = 0; win < num_windows; ++win) {
        for (int sfb = 0; sfb < num_sfb; ++sfb) {
            int start = sfb_offset[sfb];
            int end = sfb_offset[sfb + 1];
            if (end > num_lines) end = num_lines;

            bool has_data = r.read_bit() != 0;
            if (!has_data) {
                for (int i = start; i < end; ++i)
                    quant_spectral[win * num_lines + i] = 0;
                continue;
            }

            for (int idx = start; idx < end; idx += 2) {
                bool is_nonzero = r.read_bit() != 0;
                if (!is_nonzero) {
                    quant_spectral[win * num_lines + idx] = 0;
                    if (idx + 1 < num_lines)
                        quant_spectral[win * num_lines + idx + 1] = 0;
                    continue;
                }

                int bp_enc = static_cast<int>(r.read_bits(4));
                int bp = bp_enc + 1;

                uint32_t mask = (1u << bp) - 1;
                int v1_abs = static_cast<int>(r.read_bits(bp)) & static_cast<int>(mask);
                int v2_abs = static_cast<int>(r.read_bits(bp)) & static_cast<int>(mask);

                int v1_sign = 1;
                int v2_sign = 1;
                if (v1_abs > 0) v1_sign = r.read_bit() ? 1 : -1;
                if (v2_abs > 0) v2_sign = r.read_bit() ? 1 : -1;

                quant_spectral[win * num_lines + idx] = v1_abs * v1_sign;
                if (idx + 1 < num_lines)
                    quant_spectral[win * num_lines + idx + 1] = v2_abs * v2_sign;
            }
        }
    }
}

// ── ics_info writer (used by encoder) ──

static void write_ics_info(AacBitWriter& w, bool use_short) {
    w.write_bits(0, 1);   // reserved
    w.write_bits(use_short ? 1 : 0, 1);   // window_sequence
    w.write_bits(0, 1);   // window_shape (0 = sine)
    w.write_bits(0, 1);   // predictor_data_present (0)
    w.write_bits(use_short ? NUM_SFB_SHORT_44K - 1 : NUM_SFB_LONG_44K - 1,
                 use_short ? 4 : 6);
}

// ── ics_info reader (used by decoder) ──

struct IcsInfo {
    bool use_short = false;
    int max_sfb = 0;
};

static IcsInfo read_ics_info(AacBitReader& r) {
    IcsInfo info;
    r.read_bits(1);  // reserved
    info.use_short = r.read_bit() != 0;
    r.read_bits(1);  // window_shape
    r.read_bits(1);  // predictor_data_present
    info.max_sfb = static_cast<int>(r.read_bits(info.use_short ? 4 : 6)) + 1;

    if (info.use_short) {
        if (info.max_sfb > NUM_SFB_SHORT_44K) info.max_sfb = NUM_SFB_SHORT_44K;
    } else {
        if (info.max_sfb > NUM_SFB_LONG_44K) info.max_sfb = NUM_SFB_LONG_44K;
    }
    return info;
}

// ── Encode one AAC frame ──

struct AacFrame {
    int sample_rate;
    int num_channels;
    int bitrate_kbps;
    std::vector<double> pcm[2];
};

static std::vector<uint8_t> encode_frame(const AacFrame& frame) {
    int sr = frame.sample_rate;
    int channels = frame.num_channels;
    int bitrate = frame.bitrate_kbps;

    bool use_short = false;
    const int* sfb_offsets = SFB_OFFSET_LONG_44K;
    int num_sfb = NUM_SFB_LONG_44K;

    if (frame.pcm[0].size() >= 2048) {
        bool is_cold_start = true;
        for (int i = 0; i < 1024; ++i) {
            if (std::abs(frame.pcm[0][i]) > 1e-9) {
                is_cold_start = false;
                break;
            }
        }
        if (!is_cold_start) {
            double trans_test[2048] = {0};
            int copy_len = static_cast<int>(std::min<size_t>(frame.pcm[0].size(), 2048));
            for (int i = 0; i < copy_len; ++i) trans_test[i] = frame.pcm[0][i];
            use_short = detect_transient(trans_test, 2048, 8);
        }
    }

    int spectral_lines = use_short ? SPECTRAL_LINES_SHORT : SPECTRAL_LINES_LONG;
    int num_windows = use_short ? NUM_SHORT_WINDOWS : 1;

    if (use_short) {
        sfb_offsets = SFB_OFFSET_SHORT_44K;
        num_sfb = NUM_SFB_SHORT_44K;
    }

    // MDCT per channel
    double mdct_out[2][SPECTRAL_LINES_LONG] = {{0}};

    for (int ch = 0; ch < channels && ch < 2; ++ch) {
        if (use_short) {
            for (int win = 0; win < NUM_SHORT_WINDOWS; ++win) {
                double window_buf[256] = {0};
                double windowed[256] = {0};
                double spectrum[128];

                for (int i = 0; i < 128; ++i) {
                    int idx = win * 128 + i;
                    if (idx < static_cast<int>(frame.pcm[ch].size()))
                        window_buf[i] = frame.pcm[ch][idx];
                    if (idx + 128 < static_cast<int>(frame.pcm[ch].size()))
                        window_buf[128 + i] = frame.pcm[ch][idx + 128];
                }

                sine_window(window_buf, windowed, 256);
                mdct(windowed, spectrum, 256);

                for (int i = 0; i < 128; ++i)
                    mdct_out[ch][win * 128 + i] = spectrum[i];
            }
        } else {
            double window_buf[2048] = {0};
            double windowed[2048] = {0};
            double spectrum[1024];

            for (int i = 0; i < 1024; ++i) {
                if (i < static_cast<int>(frame.pcm[ch].size()))
                    window_buf[i] = frame.pcm[ch][i];
                if (i + 1024 < static_cast<int>(frame.pcm[ch].size()))
                    window_buf[1024 + i] = frame.pcm[ch][i + 1024];
            }

            sine_window(window_buf, windowed, 2048);
            mdct(windowed, spectrum, 2048);

            for (int i = 0; i < 1024; ++i)
                mdct_out[ch][i] = spectrum[i];
        }
    }

    const int* bsfb_offsets = use_short ? SFB_OFFSET_SHORT_44K : SFB_OFFSET_LONG_44K;
    int bnum_sfb = use_short ? NUM_SFB_SHORT_44K : NUM_SFB_LONG_44K;
    int bspec_lines = use_short ? SPECTRAL_LINES_SHORT : SPECTRAL_LINES_LONG;

    double scale = estimate_scale(bitrate, sr, channels);
    int global_gain = scale_to_global_gain(scale);

    // Quantize
    int quantized[2][SPECTRAL_LINES_LONG] = {{0}};

    for (int ch = 0; ch < channels && ch < 2; ++ch) {
        for (int i = 0; i < bspec_lines; ++i) {
            quantized[ch][i] = quantize(mdct_out[ch][i], scale);
        }
    }

    // ── Build raw_data_block() ──
    AacBitWriter raw;

    if (channels <= 2) {
        if (channels >= 2) {
            // CPE: channel_pair_element()
            raw.write_bits(2, 3);    // ID = 2 (CPE)
            raw.write_bits(0, 4);    // element_instance_tag
            raw.write_bits(0, 1);    // common_window

            write_ics_info(raw, use_short);
            raw.write_bits(static_cast<uint32_t>(global_gain), 8);

            write_spectral_data_simple(raw, quantized[0], bspec_lines,
                                       bsfb_offsets, bnum_sfb, num_windows);

            // Individual ics_info for channel 2
            write_ics_info(raw, use_short);
            raw.write_bits(static_cast<uint32_t>(global_gain), 8);

            write_spectral_data_simple(raw, quantized[1], bspec_lines,
                                       bsfb_offsets, bnum_sfb, num_windows);
        } else {
            // SCE: single_channel_element()
            raw.write_bits(1, 3);    // ID = 1 (SCE)
            raw.write_bits(0, 4);    // element_instance_tag

            write_ics_info(raw, use_short);
            raw.write_bits(static_cast<uint32_t>(global_gain), 8);

            write_spectral_data_simple(raw, quantized[0], bspec_lines,
                                       bsfb_offsets, bnum_sfb, num_windows);
        }
    }

    raw.write_bits(0, 3);  // ID_END terminator

    raw.flush();
    if (raw.size() == 0) raw.write_byte(0);
    while (raw.size() < 8) raw.write_byte(0);

    auto raw_data = raw.take_buffer();
    int frame_length = 7 + static_cast<int>(raw_data.size());

    // ADTS header
    AacBitWriter adts;
    adts.write_bits(0xFFF, 12);
    adts.write_bits(0, 1);
    adts.write_bits(0, 2);
    adts.write_bits(1, 1);
    adts.write_bits(1, 2);           // AAC-LC profile
    adts.write_bits(get_sampling_index(sr), 4);
    adts.write_bits(0, 1);
    adts.write_bits(channels, 3);
    adts.write_bits(0, 1);
    adts.write_bits(0, 1);
    adts.write_bits(0, 1);
    adts.write_bits(0, 1);
    adts.write_bits(frame_length, 13);
    adts.write_bits(0x7FF, 11);
    adts.write_bits(0, 2);
    adts.flush();

    auto header_bytes = adts.take_buffer();
    std::vector<uint8_t> output;
    output.reserve(header_bytes.size() + raw_data.size());
    output.insert(output.end(), header_bytes.begin(), header_bytes.end());
    output.insert(output.end(), raw_data.begin(), raw_data.end());

    return output;
}

// ── Public encoder API ──

auto encode(const std::vector<int16_t>& samples, int sample_rate,
            int num_channels, int bits_per_sample,
            int bitrate_kbps) -> std::vector<uint8_t> {
    (void)bits_per_sample;

    if (samples.empty() || sample_rate <= 0 || num_channels <= 0)
        return {};

    int samples_per_channel = static_cast<int>(samples.size()) / num_channels;
    int num_frames = (samples_per_channel + FRAME_SIZE - 1) / FRAME_SIZE;

    std::vector<uint8_t> output;

    for (int frame_idx = 0; frame_idx < num_frames; ++frame_idx) {
        AacFrame frame;
        frame.sample_rate = sample_rate;
        frame.num_channels = num_channels;
        frame.bitrate_kbps = bitrate_kbps;

        // MDCT with 2048-point window and 50% overlap: each frame needs 2048
        // samples but advances by 1024. Frame 0 starts at -1024 (padded with 0).
        int base_sample = frame_idx * FRAME_SIZE - FRAME_SIZE;

        for (int ch = 0; ch < num_channels && ch < 2; ++ch) {
            frame.pcm[ch].resize(BLOCK_LEN_LONG, 0.0);
            for (int i = 0; i < BLOCK_LEN_LONG; ++i) {
                int sample_idx = base_sample + i;
                if (sample_idx >= 0 && sample_idx < samples_per_channel) {
                    frame.pcm[ch][i] = static_cast<double>(
                        samples[sample_idx * num_channels + ch]);
                }
            }
        }

        auto frame_data = encode_frame(frame);
        if (frame_data.empty()) continue;
        output.insert(output.end(), frame_data.begin(), frame_data.end());
    }

    return output;
}

// ═══════════════════════════════════════════════════════════
// ── Decoder ──
// ═══════════════════════════════════════════════════════════

struct AacDecoderState {
    double overlap[2][1024] = {{0}}; // overlap buffer from previous frame (per channel)
    int sample_rate = 44100;
    int num_channels = 1;
    bool initialized = false;
};

// Decode one raw_data_block into quantized spectrum
static bool decode_raw_data_block(AacBitReader& r, int& num_channels,
                                   bool& use_short, int& global_gain,
                                   int quantized[2][1024],
                                   const int*& sfb_offsets, int& num_sfb) {
    // Read element_id
    if (r.bits_left() < 3) return false;

    int el_id = static_cast<int>(r.read_bits(3));
    if (el_id == 0) return false; // ID_END, no more elements

    if (el_id == 1) {
        // SCE
        num_channels = 1;
        r.read_bits(4);  // element_instance_tag

        IcsInfo ics = read_ics_info(r);
        use_short = ics.use_short;

        global_gain = static_cast<int>(r.read_bits(8));

        if (use_short) {
            sfb_offsets = SFB_OFFSET_SHORT_44K;
            num_sfb = ics.max_sfb;
        } else {
            sfb_offsets = SFB_OFFSET_LONG_44K;
            num_sfb = ics.max_sfb;
        }

        int num_windows = use_short ? NUM_SHORT_WINDOWS : 1;
        int num_lines = use_short ? 128 : 1024;

        for (int i = 0; i < 1024; ++i) quantized[0][i] = 0;
        read_spectral_data_simple(r, quantized[0], num_lines,
                                  sfb_offsets, num_sfb, num_windows);
        return true;
    }

    if (el_id == 2) {
        // CPE
        num_channels = 2;
        r.read_bits(4);  // element_instance_tag
        r.read_bits(1);  // common_window

        // Channel 0
        IcsInfo ics0 = read_ics_info(r);
        use_short = ics0.use_short;

        global_gain = static_cast<int>(r.read_bits(8));

        if (use_short) {
            sfb_offsets = SFB_OFFSET_SHORT_44K;
            num_sfb = ics0.max_sfb;
        } else {
            sfb_offsets = SFB_OFFSET_LONG_44K;
            num_sfb = ics0.max_sfb;
        }

        int num_windows = use_short ? NUM_SHORT_WINDOWS : 1;
        int num_lines = use_short ? 128 : 1024;

        for (int i = 0; i < 1024; ++i) quantized[0][i] = 0;
        read_spectral_data_simple(r, quantized[0], num_lines,
                                  sfb_offsets, num_sfb, num_windows);

        // Channel 1 (individual)
        IcsInfo ics1 = read_ics_info(r);
        int gg1 = static_cast<int>(r.read_bits(8));
        (void)gg1;

        int num_sfb1 = use_short ?
            std::min(ics1.max_sfb, NUM_SFB_SHORT_44K) :
            std::min(ics1.max_sfb, NUM_SFB_LONG_44K);
        (void)num_sfb1;

        for (int i = 0; i < 1024; ++i) quantized[1][i] = 0;
        read_spectral_data_simple(r, quantized[1], num_lines,
                                  sfb_offsets, num_sfb, num_windows);
        return true;
    }

    return false;
}

// Decode one complete AAC frame (ADTS header + raw data)
// Returns PCM samples (1024 per channel)
static bool decode_one_frame(const uint8_t* data, size_t size,
                              AacDecoderState& state,
                              std::vector<int16_t>& out_samples) {
    if (size < 7) return false;

    // ── Parse ADTS header ──
    if (data[0] != 0xFF || (data[1] & 0xF0) != 0xF0) {
        return false;
    }

    int profile = (data[1] >> 6) & 0x03;
    (void)profile;
    int sr_idx = (data[2] >> 2) & 0x0F;
    int ch_idx = ((data[2] & 0x01) << 2) | ((data[3] >> 6) & 0x03);

    int sample_rate = sampling_index_to_rate(sr_idx);
    int num_channels = ch_idx;
    if (num_channels == 0) num_channels = 1;

    int frame_length = ((data[3] & 0x03) << 11) | (data[4] << 3) | ((data[5] >> 5) & 0x07);

    if (frame_length < 7 || static_cast<size_t>(frame_length) > size)
        return false;

    // ── Decode raw_data_block ──
    AacBitReader r(data + 7, static_cast<size_t>(frame_length - 7));

    bool use_short = false;
    int global_gain = 128;
    int quantized[2][1024] = {{0}};
    const int* sfb_offsets = SFB_OFFSET_LONG_44K;
    int num_sfb = NUM_SFB_LONG_44K;

    bool ok = decode_raw_data_block(r, num_channels, use_short, global_gain,
                                     quantized, sfb_offsets, num_sfb);

    // Skip terminator and any trailing data
    if (r.bits_left() >= 3) {
        int term = static_cast<int>(r.read_bits(3));
        (void)term;
    }

    if (!ok) return false;

    // ── Dequantize ──
    double scale = global_gain_to_scale(global_gain);
    double inv_scale = 1.0 / std::max(scale, 1e-12);
    double spectrum[2][1024] = {{0}};

    int num_lines = use_short ? 128 : 1024;

    for (int ch = 0; ch < num_channels && ch < 2; ++ch) {
        if (use_short) {
            // 8 short windows: dequantize directly into spectrum
            for (int win = 0; win < NUM_SHORT_WINDOWS; ++win) {
                for (int i = 0; i < 128; ++i) {
                    int idx = win * 128 + i;
                    spectrum[ch][idx] = dequantize(quantized[ch][idx], inv_scale);
                }
            }
        } else {
            for (int i = 0; i < 1024; ++i)
                spectrum[ch][i] = dequantize(quantized[ch][i], inv_scale);
        }
    }

    // ── IMDCT + window + overlap-add ──
    // For long blocks: 2048-point IMDCT from 1024 spectral lines
    // For short blocks: 256-point IMDCT from 128 spectral lines per window

    out_samples.clear();
    out_samples.resize(FRAME_SIZE * num_channels, 0);

    for (int ch = 0; ch < num_channels && ch < 2; ++ch) {
        double imdct_out[2048] = {0};

        if (use_short) {
            // 8 short blocks of 256-point IMDCT each
            for (int win = 0; win < NUM_SHORT_WINDOWS; ++win) {
                double short_spec[128];
                double short_imdct[256];
                for (int i = 0; i < 128; ++i)
                    short_spec[i] = spectrum[ch][win * 128 + i];

                imdct(short_spec, short_imdct, 256);

                // Apply sine window
                for (int i = 0; i < 256; ++i) {
                    double w = std::sin(M_PI * (i + 0.5) / 256.0);
                    short_imdct[i] *= w;

                    int out_pos = win * 128 + i;
                    if (out_pos < 2048) imdct_out[out_pos] += short_imdct[i];
                }
            }
        } else {
            // Long block: 2048-point IMDCT
            imdct(spectrum[ch], imdct_out, 2048);

            // Apply sine window
            for (int i = 0; i < 2048; ++i) {
                double w = std::sin(M_PI * (i + 0.5) / 2048.0);
                imdct_out[i] *= w;
            }
        }

        // Overlap-add: first half with previous frame overlap
        for (int i = 0; i < 1024; ++i) {
            double sample = imdct_out[i] + state.overlap[ch][i];
            // Clamp to int16 range
            int sample_int = static_cast<int>(std::round(sample));
            sample_int = std::max(-32768, std::min(32767, sample_int));
            out_samples[i * num_channels + ch] = static_cast<int16_t>(sample_int);
        }

        // Save second half for next frame overlap
        for (int i = 0; i < 1024; ++i)
            state.overlap[ch][i] = imdct_out[1024 + i];
    }

    // Fill channel 2 with channel 1 if mono decoded as stereo
    // (already handled by channel loop)

    state.sample_rate = sample_rate;
    state.num_channels = num_channels;
    state.initialized = true;

    return true;
}

// ── Public decoder API ──

auto decode(const std::vector<uint8_t>& aac_data) -> std::vector<int16_t> {
    if (aac_data.empty()) return {};

    AacDecoderState state;
    std::vector<int16_t> output;

    size_t pos = 0;
    while (pos + 7 <= aac_data.size()) {
        const uint8_t* ptr = aac_data.data() + pos;

        // Find ADTS syncword
        if (ptr[0] != 0xFF || (ptr[1] & 0xF0) != 0xF0) {
            pos++;
            continue;
        }

        // Parse frame length
        int frame_length = ((ptr[3] & 0x03) << 11) | (ptr[4] << 3) | ((ptr[5] >> 5) & 0x07);
        if (frame_length < 7) { pos++; continue; }
        if (pos + static_cast<size_t>(frame_length) > aac_data.size()) break;

        std::vector<int16_t> frame_samples;
        if (decode_one_frame(ptr, static_cast<size_t>(frame_length),
                              state, frame_samples)) {
            output.insert(output.end(), frame_samples.begin(), frame_samples.end());
        }

        pos += static_cast<size_t>(frame_length);
    }

    return output;
}

} // namespace compressor::algorithm::aac
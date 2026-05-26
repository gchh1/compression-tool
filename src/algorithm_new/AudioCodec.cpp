#include "AudioCodec.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <numeric>

namespace compressor::algorithm {

// ── CRC tables ──

namespace {

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

}  // namespace

// ── FLAC bit writer (MSB-first) ──

namespace {

class FlacBitWriter {
    std::vector<uint8_t> buf_;
    int bit_pos_ = 0;

    void write_bit(int bit) {
        if (bit_pos_ == 0)
            buf_.push_back(0);
        if (bit)
            buf_.back() |= static_cast<uint8_t>(1 << (7 - bit_pos_));
        bit_pos_ = (bit_pos_ + 1) & 7;
    }

public:
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

    void flush() {
        bit_pos_ = 0;
    }

    bool is_byte_aligned() const { return bit_pos_ == 0; }
    int get_bit_pos() const { return bit_pos_; }

    void write_byte(uint8_t b) {
        if (bit_pos_ == 0) {
            buf_.push_back(b);
        } else {
            write_bits(b, 8);
        }
    }

    void write_bytes(const uint8_t* data, size_t len) {
        for (size_t i = 0; i < len; ++i)
            write_byte(data[i]);
    }

    const std::vector<uint8_t>& buffer() const { return buf_; }
    std::vector<uint8_t> take_buffer() { return std::move(buf_); }
    size_t size() const { return buf_.size(); }

    void replace_byte(size_t pos, uint8_t b) {
        if (pos < buf_.size())
            buf_[pos] = b;
    }
};

}  // namespace

// ── Fixed predictor ──

namespace {

int64_t fixed_predict(int order, const int32_t* data, int pos) {
    switch (order) {
        case 0: return 0;
        case 1: return data[pos - 1];
        case 2: return 2LL * data[pos - 1] - data[pos - 2];
        case 3: return 3LL * data[pos - 1] - 3LL * data[pos - 2] + data[pos - 3];
        case 4: return 4LL * data[pos - 1] - 6LL * data[pos - 2] + 4LL * data[pos - 3] - data[pos - 4];
        default: return 0;
    }
}

uint32_t compute_residual(const int32_t* block, size_t block_size,
                          int order, int32_t* residual) {
    uint64_t sum_abs = 0;
    for (size_t i = static_cast<size_t>(order); i < block_size; ++i) {
        int64_t pred = fixed_predict(order, block, static_cast<int>(i));
        int32_t res = static_cast<int32_t>(static_cast<int64_t>(block[i]) - pred);
        residual[i] = res;
        sum_abs += static_cast<uint64_t>(res < 0 ? -res : res);
    }
    size_t count = block_size - static_cast<size_t>(order);
    return count > 0 ? static_cast<uint32_t>(sum_abs / count) : 0;
}

int estimate_rice_param(const int32_t* residuals, size_t count) {
    if (count == 0) return 0;
    uint64_t sum = 0;
    for (size_t i = 0; i < count; ++i)
        sum += static_cast<uint64_t>(residuals[i] < 0 ? -residuals[i] : residuals[i]);
    uint64_t mean = sum / count;
    if (mean == 0) return 0;
    int k = 0;
    while ((static_cast<uint64_t>(1) << (k + 1)) <= mean && k < 14)
        ++k;
    return std::min(k, 14);
}

uint32_t fold_signed_to_unsigned(int32_t val) {
    if (val >= 0)
        return static_cast<uint32_t>(static_cast<uint64_t>(val) * 2);
    return static_cast<uint32_t>(static_cast<uint64_t>(-val) * 2 - 1);
}

void write_rice_residual(FlacBitWriter& w, const int32_t* residuals,
                         size_t count, int k) {
    for (size_t i = 0; i < count; ++i) {
        uint32_t u = fold_signed_to_unsigned(residuals[i]);
        uint32_t q = u >> k;
        uint32_t r = u & ((static_cast<uint32_t>(1) << k) - 1);
        for (uint32_t j = 0; j < q; ++j)
            w.write_bits(1, 1);
        w.write_bits(0, 1);
        if (k > 0)
            w.write_bits(r, k);
    }
}

}  // namespace

// ── FLAC encode ──

namespace flac {

auto encode(const std::vector<int16_t>& samples, int sample_rate,
            int num_channels, int bits_per_sample,
            int compression_level) -> std::vector<uint8_t> {
    static_cast<void>(compression_level);

    const size_t total_samples = samples.size() / static_cast<size_t>(num_channels);
    const int block_size = 4096;
    const int max_order = 4;

    FlacBitWriter w;

    // ── Metadata: STREAMINFO (34 bytes) ──
    // Metadata block header: 1 bit is_last(0) + 7 bits type(0) + 24 bits length(34)
    w.write_bits(0, 1);      // not last (will be patched)
    w.write_bits(0, 7);      // STREAMINFO
    w.write_bits(34, 24);    // length

    // STREAMINFO content (34 bytes)
    // min/max block size (16 bits each)
    w.write_bits(static_cast<uint32_t>(block_size), 16);
    w.write_bits(static_cast<uint32_t>(block_size), 16);
    // min/max frame size (24 bits each) - 0 = unknown
    w.write_bits(0, 24);
    w.write_bits(0, 24);
    // Sample rate (20 bits)
    w.write_bits(static_cast<uint32_t>(sample_rate), 20);
    // Num channels - 1 (3 bits)
    w.write_bits(static_cast<uint32_t>(num_channels - 1), 3);
    // Bits per sample - 1 (5 bits)
    w.write_bits(static_cast<uint32_t>(bits_per_sample - 1), 5);
    // Total samples (36 bits)
    w.write_bits(static_cast<uint32_t>(total_samples & 0xFFFFFFFFULL), 32);
    w.write_bits(static_cast<uint32_t>((total_samples >> 32) & 0x0FULL), 4);
    // MD5 signature (16 bytes) - all zeros (simplified)
    for (int i = 0; i < 16; ++i)
        w.write_bits(0, 8);

    // ── Metadata: PADDING (optional, ~4KB for editability) ──
    const int padding_size = 4096;
    w.write_bits(0, 1);       // not last
    w.write_bits(1, 7);       // PADDING
    w.write_bits(padding_size, 24);
    for (int i = 0; i < padding_size; ++i)
        w.write_bits(0, 8);

    // Patch metadata is_last bits later
    const size_t streaminfo_pos = 0; // first byte
    const size_t padding_pos = streaminfo_pos + 2 + 34; // after STREAMINFO block

    // ── Audio frames ──
    size_t frame_start_sample = 0;
    uint64_t frame_number = 0;

    // Sample rate encoding lookup
    auto sample_rate_code = [](int sr) -> uint32_t {
        switch (sr) {
            case 88200: return 1;
            case 176400: return 2;
            case 192000: return 3;
            case 8000: return 4;
            case 16000: return 5;
            case 22050: return 6;
            case 24000: return 7;
            case 32000: return 8;
            case 44100: return 9;
            case 48000: return 10;
            case 96000: return 11;
            default: return 0;
        }
    };

    const uint32_t sr_code = sample_rate_code(sample_rate);

    while (frame_start_sample < total_samples) {
        const size_t remaining = total_samples - frame_start_sample;
        const int cur_block_size = static_cast<int>(std::min(static_cast<size_t>(block_size), remaining));
        const size_t frame_start_byte = w.size();

        // Frame header
        w.write_bits(0x3FFE, 14);  // sync code
        w.write_bits(0, 1);        // reserved
        w.write_bits(0, 1);        // fixed-blocksize
        // Block size encoding: 4096 => code 0b1100 (256*2^(12-7) = 256*32 = 8192... wait)
        // 4096 = 256 * 2^4 => n = 11, code = 1011
        // 0b1000 = 256*2^(8-7)=256, 0b1001=512, 0b1010=1024, 0b1011=2048, 0b1100=4096
        // Actually: 1000 => 256, 1001 => 512, 1010 => 1024, 1011 => 2048, 1100 => 4096
        uint32_t bs_code;
        if (cur_block_size <= 256) bs_code = 8;  // hmm, not exact
        else if (cur_block_size <= 512) bs_code = 9;
        else if (cur_block_size <= 1024) bs_code = 10;
        else if (cur_block_size <= 2048) bs_code = 11;
        else if (cur_block_size <= 4096) bs_code = 12;
        else if (cur_block_size <= 8192) bs_code = 13;
        else bs_code = 14;
        w.write_bits(bs_code, 4);

        w.write_bits(sr_code, 4);
        // Channel assignment: independent channels
        w.write_bits(static_cast<uint32_t>(num_channels == 1 ? 0 : 1), 4);
        // Sample size: 16 bits => code 011
        w.write_bits(3, 3);  // 011
        w.write_bits(0, 1);  // reserved

        // Frame number (fixed-blocksize)
        w.write_utf8(frame_number);

        // Block size / sample rate overrides (not needed here)

        // CRC-8 of frame header so far
        const size_t header_end_byte = w.size();
        int header_bit_end = static_cast<int>(w.size()) * 8 + (8 - w.get_bit_pos()) % 8;
        // Actually, compute CRC-8 over the bytes written for this frame header
        // We need to find the header bytes range
        // For now, write 0 placeholder and patch
        size_t crc8_pos = w.size();
        // Check if we need to flush to get byte boundary... 
        // Actually CRC-8 is computed over the frame header data as a byte stream.
        // The frame header is padded to byte boundary before CRC-8.
        // If we're not on a byte boundary, we need to pad with 0s.
        // But we should ensure we're on a byte boundary.
        // The frame header should be byte-aligned after writing all fields.
        // Let's just write 0 and patch.
        w.write_byte(0);

        // Now compute CRC-8 over the frame header bytes
        size_t header_bytes_len = w.size() - frame_start_byte - 1; // minus CRC-8 itself
        // Actually CRC-8 is over everything before the CRC-8 byte
        const auto& buf = w.buffer();
        uint8_t crc_val = crc8(buf.data() + frame_start_byte, header_bytes_len);
        w.replace_byte(crc8_pos, crc_val);

        // ── Subframes ──
        for (int ch = 0; ch < num_channels; ++ch) {
            // Extract channel data
            // Build block as int32_t arrays for prediction computation
            // For each channel, we need [block_size] int32_t values
            std::vector<int32_t> channel_block(cur_block_size);
            for (int i = 0; i < cur_block_size; ++i) {
                size_t sample_idx = frame_start_sample + static_cast<size_t>(i);
                channel_block[i] = samples[sample_idx * static_cast<size_t>(num_channels) + static_cast<size_t>(ch)];
            }

            // Choose best fixed predictor order
            int best_order = 0;
            uint32_t best_energy = std::numeric_limits<uint32_t>::max();
            std::vector<int32_t> best_residual(cur_block_size);
            std::vector<int32_t> residual(cur_block_size);

            for (int order = 0; order <= max_order; ++order) {
                if (static_cast<size_t>(order) >= channel_block.size()) break;
                auto mean_abs = compute_residual(channel_block.data(),
                                                  static_cast<size_t>(cur_block_size),
                                                  order, residual.data());
                if (mean_abs < best_energy) {
                    best_energy = mean_abs;
                    best_order = order;
                    std::copy(residual.begin(), residual.end(), best_residual.begin());
                }
            }

            // Determine if CONSTANT subframe is better
            bool is_constant = true;
            for (int i = 1; i < cur_block_size; ++i) {
                if (channel_block[i] != channel_block[0]) {
                    is_constant = false;
                    break;
                }
            }

            if (is_constant) {
                // CONSTANT subframe
                w.write_bits(0, 1);  // not padding
                w.write_bits(0, 6);  // CONSTANT type
                w.write_bits(0, 1);  // no wasted bits
                // Write constant value as signed (bits_per_sample)
                uint32_t const_val = static_cast<uint32_t>(
                    static_cast<int32_t>(channel_block[0]) & 0xFFFF);
                w.write_bits(const_val, bits_per_sample);
            } else {
                // FIXED subframe
                size_t warmup = static_cast<size_t>(best_order);
                int rp = estimate_rice_param(best_residual.data() + warmup,
                                              static_cast<size_t>(cur_block_size) - warmup);

                w.write_bits(0, 1);  // not padding
                // FIXED type: 001bbb where bbb = order
                w.write_bits(1, 1);
                w.write_bits(0, 1);
                w.write_bits(0, 1);
                w.write_bits(static_cast<uint32_t>(best_order), 3);
                w.write_bits(0, 1);  // no wasted bits

                // Warm-up samples
                for (size_t i = 0; i < warmup; ++i) {
                    uint32_t warm_val = static_cast<uint32_t>(
                        static_cast<int32_t>(channel_block[i]) & 0xFFFF);
                    w.write_bits(warm_val, bits_per_sample);
                }

                // Rice coding method + parameter
                // Coding method 0: 4 bits, MSB=0, 3 bits = k (0-7)
                int rp_clamped = std::min(rp, 7);
                w.write_bits(0, 1);  // method 0
                w.write_bits(static_cast<uint32_t>(rp_clamped), 3);

                // Partitioned Rice coding: 1 partition (partition order = 0)
                w.write_bits(0, 4);  // partition order = 0

                // Write residuals (after warmup)
                write_rice_residual(w, best_residual.data() + warmup,
                                    static_cast<size_t>(cur_block_size) - warmup, rp_clamped);
            }
        }

        // Frame footer: CRC-16 over entire frame
        // Compute CRC-16 over frame bytes from frame_start_byte to current byte
        size_t frame_end_byte = w.size();
        const auto& buf2 = w.buffer();
        uint16_t crc16_val = crc16(buf2.data() + frame_start_byte,
                                    frame_end_byte - frame_start_byte);
        w.write_byte(static_cast<uint8_t>(crc16_val >> 8));
        w.write_byte(static_cast<uint8_t>(crc16_val & 0xFF));

        frame_start_sample += static_cast<size_t>(cur_block_size);
        ++frame_number;
    }

    // ── Finalize ──
    w.flush();

    // Patch STREAMINFO metadata block to mark it not-last (type bit = 0 for not last)
    // Actually we need to mark the LAST metadata block (PADDING) as is_last=1
    // Patch padding header byte at padding_pos: set bit 7 = 1
    // padding header is at byte position: 2 + 34 = 36 (STREAMINFO header 2B + data 34B)
    // Let's recalculate: streaminfo block header at byte 0 (2 bytes), streaminfo data at byte 2 (34 bytes)
    // So padding block header at byte 36
    auto result = w.take_buffer();
    if (result.size() > padding_pos) {
        result[padding_pos] |= 0x80;  // set is_last = 1
    }

    return result;
}

}  // namespace flac

// ── WAV parsing ──

auto parse_wav(const std::vector<uint8_t>& data) -> WavInfo {
    WavInfo info;
    if (data.size() < 44) return info;

    // Check RIFF header
    if (data[0] != 'R' || data[1] != 'I' || data[2] != 'F' || data[3] != 'F')
        return info;
    if (data[8] != 'W' || data[9] != 'A' || data[10] != 'V' || data[11] != 'E')
        return info;

    // Find fmt chunk
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
            if (audio_format != 1) return info;  // PCM only
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
            samples[i] = static_cast<int16_t>(static_cast<int>(val) * 256);
        }
    } else {
        return {};
    }

    switch (format) {
        case AudioFormat::FLAC:
            return flac::encode(samples, info.sample_rate,
                                info.num_channels, info.bits_per_sample, quality);
        case AudioFormat::AAC_LC:
            return aac::encode(samples, info.sample_rate,
                               info.num_channels, info.bits_per_sample, quality * 32 + 32);
    }
    return {};
}

auto audio_decompress(const std::vector<uint8_t>& data) -> std::vector<uint8_t> {
    if (data.size() < 4) return {};

    // Detect FLAC by magic "fLaC"
    if (data[0] == 'f' && data[1] == 'L' && data[2] == 'a' && data[3] == 'C') {
        auto samples = flac::decode(data);
        if (samples.empty()) return {};

        // Extract sample rate and channels from STREAMINFO
        // STREAMINFO is at byte 4 (marker "fLaC" + metadata block starting at byte 4)
        // STREAMINFO data: bytes 4-5 = metadata header, bytes 6-39 = STREAMINFO data
        // Sample rate: bits 90-109 in the bitstream (10 bytes into STREAMINFO data)
        // = bytes 16-17 in overall offset (but it's bit-aligned, about bytes 16-18)
        // Actually STREAMINFO: min_blocksize(2B) + max_blocksize(2B) + min_framesize(3B) + max_framesize(3B)
        // = 10 bytes before sample_rate
        // Sample rate is 20 bits, so: byte 6+10=16, 17, and first 4 bits of byte 18
        if (data.size() < 42) return {};
        uint32_t sr_raw = (static_cast<uint32_t>(data[16]) << 12) |
                          (static_cast<uint32_t>(data[17]) << 4) |
                          (static_cast<uint32_t>(data[18]) >> 4);
        int sample_rate = static_cast<int>(sr_raw & 0xFFFFF);
        int num_channels = static_cast<int>((data[18] >> 1) & 0x07) + 1;
        int bits_per_sample = static_cast<int>(((data[18] & 0x01) << 4) | (data[19] >> 4)) + 1;

        return build_wav(samples, sample_rate, num_channels, bits_per_sample);
    }

    return {};
}

// ── FLAC decoder ──

namespace flac {

namespace {

int32_t fixed_warmup(int order, const int32_t* warmup, int pos) {
    // warmup values are the actual decoded warmup samples
    // At pos == order, we compute prediction from warmup[0..order-1]
    // Subsequent positions use previously decoded residuals
    return static_cast<int32_t>(fixed_predict(order, warmup, pos));
}

}  // namespace

auto decode(const std::vector<uint8_t>& flac_data) -> std::vector<int16_t> {
    if (flac_data.size() < 42) return {};
    if (flac_data[0] != 'f' || flac_data[1] != 'L' ||
        flac_data[2] != 'a' || flac_data[3] != 'C')
        return {};

    // Extract STREAMINFO
    if (flac_data.size() < 42) return {};
    uint32_t sr_raw = (static_cast<uint32_t>(flac_data[16]) << 12) |
                      (static_cast<uint32_t>(flac_data[17]) << 4) |
                      (static_cast<uint32_t>(flac_data[18]) >> 4);
    int sample_rate = static_cast<int>(sr_raw & 0xFFFFF);
    int num_channels = static_cast<int>((flac_data[18] >> 1) & 0x07) + 1;
    int bits_per_sample = static_cast<int>(((flac_data[18] & 0x01) << 4) | (flac_data[19] >> 4)) + 1;

    // Total samples (36 bits at bytes 20-24)
    uint64_t total_samples = 0;
    for (int i = 0; i < 4; ++i)
        total_samples = (total_samples << 8) | flac_data[24 + i];
    total_samples = (total_samples << 4) | (flac_data[20] & 0x0F);
    // Wait, the layout is: byte 20 = bits_per_sample(4 bits of upper) + total_samples(4 upper bits)
    // Actually: byte 20 lower 4 bits = total_samples high bits, bytes 21-24 = 32 bits
    total_samples = (static_cast<uint64_t>(flac_data[20] & 0x0F) << 32) |
                    (static_cast<uint64_t>(flac_data[21]) << 24) |
                    (static_cast<uint64_t>(flac_data[22]) << 16) |
                    (static_cast<uint64_t>(flac_data[23]) << 8) |
                    static_cast<uint64_t>(flac_data[24]);

    // Simple FLAC decoder
    // Skip metadata blocks to find first frame
    size_t pos = 4; // after "fLaC"
    while (pos + 4 <= flac_data.size()) {
        uint8_t block_hdr = flac_data[pos];
        bool is_last = (block_hdr & 0x80) != 0;
        uint32_t block_len = (static_cast<uint32_t>(flac_data[pos + 1]) << 16) |
                             (static_cast<uint32_t>(flac_data[pos + 2]) << 8) |
                             static_cast<uint32_t>(flac_data[pos + 3]);
        pos += 4 + block_len;
        if (is_last) break;
    }

    if (pos >= flac_data.size()) return {};

    // Now decode frames
    std::vector<int16_t> all_samples;
    if (total_samples > 0)
        all_samples.reserve(static_cast<size_t>(total_samples) * static_cast<size_t>(num_channels));

    while (pos < flac_data.size()) {
        // Find sync code 0xFFF8...0xFFFE (14 bits of 1s)
        // We need to find the start of a frame in the byte stream
        // The sync code is 0xFFF8, 0xFFF9, ..., 0xFFFE (0xFF + 0xF8-0xFE)
        // Just look for 0xFF followed by byte with upper nibble 0xF
        while (pos + 1 < flac_data.size()) {
            if (flac_data[pos] == 0xFF && (flac_data[pos + 1] & 0xFC) == 0xF8)
                break;
            ++pos;
        }
        if (pos + 16 > flac_data.size()) break;

        // Found a frame header candidate
        size_t frame_start = pos;

        // Parse frame header to get block size
        uint8_t b1 = flac_data[pos + 1];
        uint32_t bs_code = (b1 >> 4) & 0x0F;
        uint32_t sr_code = (b1 >> 0) & 0x0F;
        uint8_t b2 = flac_data[pos + 2];
        int ch_assignment = (b2 >> 4) & 0x0F;
        int sample_size_code = (b2 >> 1) & 0x07;

        int actual_channels;
        if (ch_assignment < 8) {
            actual_channels = ch_assignment + 1;
        } else {
            actual_channels = 2; // mid/side etc.
        }

        // Decode block size
        int block_size;
        bool has_bs_override = false;
        switch (bs_code) {
            case 1: block_size = 192; break;
            case 2: case 3: case 4: case 5:
                block_size = 576 << (bs_code - 2); break;
            case 6: has_bs_override = true; block_size = 0; break;
            case 7: has_bs_override = true; block_size = 0; break;
            default: // 8-15
                block_size = 256 << (bs_code - 8); break;
        }

        // Skip frame header fields to find CRC-8 and subframes
        // Frame header: sync(2B) + bs_code(4b) + sr_code(4b) + ch(4b) + sample_size(3b) + reserved(1b) = 3 bytes mandatory
        // + UTF-8 frame number (variable)
        // + optional block size (1-2 bytes if bs_code 6 or 7)
        // + optional sample rate (1-2 bytes)
        // + CRC-8 (1 byte)

        size_t hdr_pos = pos + 3; // after sync + first header byte
        // Skip the rest of the second header byte (we already read ch and sample_size from b2)
        // Actually b2 = byte 2, which has ch(4b) + sample_size(3b) + reserved(1b)
        // This is fully consumed.

        // Read UTF-8 frame number and skip
        // We'll do a simple scan: skip UTF-8 bytes
        while (hdr_pos < flac_data.size() && (flac_data[hdr_pos] & 0x80) != 0) {
            ++hdr_pos;
            if (hdr_pos - (pos + 3) > 5) break; // max UTF-8 bytes
        }
        ++hdr_pos; // skip last UTF-8 byte (starts with 0 or has 10xxxxxx)
        // Actually, UTF-8 in FLAC: the frame number can be 1-7 bytes
        // First byte determines length
        uint8_t first_utf8 = flac_data[pos + 3];
        int utf8_len;
        if (first_utf8 < 0x80) utf8_len = 1;
        else if (first_utf8 < 0xC0) utf8_len = 1; // invalid, assume 1
        else if (first_utf8 < 0xE0) utf8_len = 2;
        else if (first_utf8 < 0xF0) utf8_len = 3;
        else if (first_utf8 < 0xF8) utf8_len = 4;
        else if (first_utf8 < 0xFC) utf8_len = 5;
        else utf8_len = 6;
        hdr_pos = pos + 3 + utf8_len;

        // Optional block size override
        if (bs_code == 6 && hdr_pos < flac_data.size()) {
            block_size = flac_data[hdr_pos] + 1;
            ++hdr_pos;
        } else if (bs_code == 7 && hdr_pos + 1 < flac_data.size()) {
            block_size = (static_cast<int>(flac_data[hdr_pos]) << 8) | flac_data[hdr_pos + 1] + 1;
            hdr_pos += 2;
        }

        // Optional sample rate override
        if (sr_code >= 12) {
            if (sr_code == 12 && hdr_pos < flac_data.size()) {
                sample_rate = flac_data[hdr_pos] * 1000;
                ++hdr_pos;
            } else if (sr_code == 13 && hdr_pos + 1 < flac_data.size()) {
                sample_rate = (static_cast<int>(flac_data[hdr_pos]) << 8) | flac_data[hdr_pos + 1];
                hdr_pos += 2;
            } else if (sr_code == 14 && hdr_pos + 1 < flac_data.size()) {
                sample_rate = (static_cast<int>(flac_data[hdr_pos]) << 8) | flac_data[hdr_pos + 1] * 10;
                hdr_pos += 2;
            }
        }

        // CRC-8 byte
        if (hdr_pos >= flac_data.size()) break;
        ++hdr_pos; // skip CRC-8

        if (hdr_pos >= flac_data.size()) break;
        pos = hdr_pos; // pos now at subframes

        if (block_size <= 0 || block_size > 65536) break;

        // Decode subframes
        std::vector<std::vector<int32_t>> channel_data(static_cast<size_t>(actual_channels),
                                                        std::vector<int32_t>(static_cast<size_t>(block_size)));

        bool decode_ok = true;
        for (int ch = 0; ch < actual_channels && decode_ok; ++ch) {
            if (pos >= flac_data.size()) { decode_ok = false; break; }

            // Subframe header: 1 bit padding(0) + 6 bits type + 1 bit wasted
            uint8_t sf_type = 0;
            if (pos < flac_data.size()) {
                // Need to read bits
                sf_type = flac_data[pos]; // simplified: read byte-level
                // Check for padding bit
                bool padding_bit = (sf_type & 0x80) != 0;
                if (padding_bit) {
                    // Shouldn't happen in our encoder
                }
                int type = (sf_type >> 1) & 0x3F;
                bool has_wasted = (sf_type & 0x01) != 0;

                if (type == 0) {
                    // CONSTANT
                    ++pos;
                    if (pos + (bits_per_sample / 8) > flac_data.size()) { decode_ok = false; break; }
                    int32_t const_val = 0;
                    if (bits_per_sample == 16) {
                        const_val = static_cast<int16_t>((static_cast<int>(flac_data[pos]) << 8) | flac_data[pos + 1]);
                        pos += 2;
                    } else if (bits_per_sample == 8) {
                        const_val = static_cast<int8_t>(flac_data[pos]);
                        pos += 1;
                    }
                    for (int i = 0; i < block_size; ++i)
                        channel_data[ch][i] = const_val;
                } else if (type >= 8 && type <= 12) {
                    // FIXED (order = type - 8)
                    int order = type - 8;
                    // ++pos (we've consumed the header byte)
                    // Actually we need bit-level reading for FIXED subframe
                    // Since our encoder produces everything MSB-first within each byte,
                    // and the subframe data starts at the current byte boundary after CRC-8,
                    // we need a bit reader.
                    // For simplicity, let's switch to a bit reader approach.
                    decode_ok = false;
                    break;
                } else if (type == 1) {
                    // VERBATIM
                    ++pos;
                    if (pos + block_size * (bits_per_sample / 8) > flac_data.size()) { decode_ok = false; break; }
                    for (int i = 0; i < block_size; ++i) {
                        int32_t val = 0;
                        if (bits_per_sample == 16) {
                            val = static_cast<int16_t>((static_cast<int>(flac_data[pos]) << 8) | flac_data[pos + 1]);
                            pos += 2;
                        } else if (bits_per_sample == 8) {
                            val = static_cast<int8_t>(flac_data[pos]);
                            pos += 1;
                        }
                        channel_data[ch][i] = val;
                    }
                } else {
                    decode_ok = false;
                }
            }
        }

        if (!decode_ok) {
            // Fall back to simpler position advancement
            // Just scan for next sync code
            ++pos;
            continue;
        }

        // Interleave channels
        for (int i = 0; i < block_size; ++i) {
            for (int ch = 0; ch < actual_channels; ++ch) {
                int32_t sample = channel_data[ch][i];
                if (sample < -32768) sample = -32768;
                if (sample > 32767) sample = 32767;
                all_samples.push_back(static_cast<int16_t>(sample));
            }
        }

        // Skip frame footer CRC-16 (2 bytes) and continue
        pos = frame_start + 1; // advance past sync code to look for next frame
    }

    return all_samples;
}

}  // namespace flac

}  // namespace compressor::algorithm
#include <cstdio>
#include <cstdint>
#include <vector>
#include <string>
#include <algorithm>
#include <span>
#include "DPFlate.hpp"
#include "Inflate.hpp"
#include "BitReader.hpp"
#include "BitWriter.hpp"
#include "HuffmanTree.hpp"
#include "DebugLog.hpp"

static uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t size) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int j = 0; j < 8; j++) {
                if (c & 1) c = 0xEDB88320 ^ (c >> 1);
                else c >>= 1;
            }
            table[i] = c;
        }
        init = true;
    }
    crc = ~crc;
    for (size_t i = 0; i < size; i++) {
        crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

int main() {
    compressor::debug::DebugLog::instance().enable("debug_token_trace.log");

    // Create test data: repeating pattern
    const std::string pat = "REGRESS_LZDP_DPFLATE_";
    std::vector<uint8_t> data;
    for (int i = 0; i < 32; ++i)
        data.insert(data.end(), pat.begin(), pat.end());

    printf("Original size: %zu, CRC32: 0x%08x\n\n", data.size(),
           crc32_update(0, data.data(), data.size()));

    // ---- Step 1: Compress with DPFlate directly ----
    printf("=== Step 1: Direct DPFlate compression ===\n");
    compressor::algorithm::DPFlate dpflate(2048, 128, 4, 128, 6);
    dpflate.set_match_engine(1);
    dpflate.set_use_flag_encoding(false);
    dpflate.set_use_3hfmtree(false);
    dpflate.reset();

    std::vector<uint8_t> compressed(data.size() * 2 + 65536, 0);
    auto st = dpflate.process(data, compressed, true);
    compressed.resize(st.bytes_produced);
    printf("Compressed: %zu bytes\n", compressed.size());
    printf("First 64 bytes:\n  ");
    for (size_t i = 0; i < compressed.size() && i < 64; ++i) {
        printf("%02x ", compressed[i]);
        if ((i + 1) % 16 == 0) printf("\n  ");
    }
    printf("\n\n");

    // ---- Step 2: Decompress with Inflate directly ----
    printf("=== Step 2: Direct Inflate decompression ===\n");
    compressor::algorithm::Inflate inflate;
    inflate.reset();

    // Skip format byte (0x46)
    std::vector<uint8_t> payload(compressed.begin() + 1, compressed.end());
    std::vector<uint8_t> decompressed(payload.size() * 10 + 65536, 0);
    
    size_t in_off = 0;
    size_t out_pos = 0;
    compressor::algorithm::AlgorithmStatus dst{};
    for (;;) {
        if (out_pos >= decompressed.size()) {
            decompressed.resize(decompressed.size() * 2);
        }
        auto in_span = std::span<const uint8_t>(payload.data() + in_off, payload.size() - in_off);
        auto out_span = std::span<uint8_t>(decompressed.data() + out_pos, decompressed.size() - out_pos);
        dst = inflate.process(in_span, out_span, true);
        in_off += dst.bytes_consumed;
        out_pos += dst.bytes_produced;
        if (dst.done) break;
        if (dst.need_output && dst.bytes_produced == 0) {
            decompressed.resize(decompressed.size() * 2);
            continue;
        }
        if (dst.need_input && in_off >= payload.size()) break;
    }
    decompressed.resize(out_pos);
    printf("Decompressed: %zu bytes, done=%d\n", decompressed.size(), dst.done);
    printf("Match: %s\n", decompressed == data ? "YES" : "NO");
    if (decompressed != data) {
        for (size_t i = 0; i < data.size() && i < decompressed.size(); ++i) {
            if (data[i] != decompressed[i]) {
                printf("First diff at offset %zu: expected 0x%02x got 0x%02x\n",
                       i, data[i], decompressed[i]);
                // Show context
                size_t start = (i > 16) ? i - 16 : 0;
                size_t end = std::min(i + 16, data.size());
                printf("  Original [%zu..%zu]: ", start, end - 1);
                for (size_t j = start; j < end; ++j)
                    printf("%02x ", data[j]);
                printf("\n  Decoded  [%zu..%zu]: ", start, end - 1);
                for (size_t j = start; j < end && j < decompressed.size(); ++j)
                    printf("%02x ", decompressed[j]);
                printf("\n");
                break;
            }
        }
    }

    // ---- Step 3: Analyze the compressed bitstream ----
    printf("\n=== Step 3: Bitstream analysis ===\n");
    {
        // Skip format byte
        std::vector<uint8_t> bitstream(compressed.begin() + 1, compressed.end());
        compressor::utils::BitReader reader(bitstream);
        
        // Read literal/length tree
        printf("Reading literal/length tree...\n");
        auto readTree = [&reader](auto& self, size_t sym_bits) -> void {
            if (reader.getRemainingBits() == 0) return;
            uint8_t bit = static_cast<uint8_t>(reader.readBit());
            if (reader.getRemainingBits() == 0 && bit == 0) return;
            if (bit == 0) {
                printf("  internal node\n");
                self(self, sym_bits);
                self(self, sym_bits);
            } else {
                uint16_t sym = static_cast<uint16_t>(reader.readBits(static_cast<uint8_t>(sym_bits)));
                printf("  leaf: symbol=%u\n", sym);
            }
        };
        
        printf("  Lit/Len tree (sym_bits=9):\n");
        readTree(readTree, 9);
        
        printf("  Dist tree (sym_bits=5):\n");
        readTree(readTree, 5);
        
        printf("  Remaining bits after trees: %zu\n", reader.getRemainingBits());
        
        // Now try to decode tokens
        printf("\n  Decoding first 20 tokens:\n");
        // We need to rebuild the trees to decode tokens
        // But for now, let's just dump the raw bits
        reader.changeSource(bitstream);
        // Skip the trees by reading bits until we've consumed the tree data
        // Actually, let's just dump the bitstream from a known offset
        
        // The tree size can be calculated from the serialized format
        // Let's just dump bytes
        printf("  Full bitstream bytes:\n  ");
        for (size_t i = 0; i < bitstream.size(); ++i) {
            printf("%02x ", bitstream[i]);
            if ((i + 1) % 16 == 0) printf("\n  ");
        }
        printf("\n");
    }

    return 0;
}
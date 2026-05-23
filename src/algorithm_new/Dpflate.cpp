#include "Dpflate.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <queue>
#include <utility>

#include "HuffmanTree.hpp"
#include "Inflate3HMCoding.hpp"
#include "InflateCoding.hpp"
#include "LZDP.hpp"
#include "StreamingCancel.hpp"

namespace {

static constexpr uint16_t kLengthBases[] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static constexpr uint8_t kLengthExtra[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static constexpr size_t kLengthCodeCount = 29;

static constexpr uint16_t kDistBases[] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577
};
static constexpr uint8_t kDistExtra[] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};
static constexpr size_t kDistCodeCount = 30;
static constexpr size_t kDeflateAlphabet = 286;
static constexpr size_t kDeflateSymbolBits = 9;
static constexpr size_t kDistanceAlphabet = 30;
static constexpr size_t kDistanceSymbolBits = 5;

struct LSBWriter {
    std::vector<uint8_t>& out;
    uint64_t buf{0};
    int bits{0};

    void writeBit(uint8_t bit) {
        buf |= (static_cast<uint64_t>(bit & 1) << bits);
        bits++;
        if (bits == 8) {
            out.push_back(static_cast<uint8_t>(buf));
            buf >>= 8;
            bits = 0;
        }
    }

    void writeBits(uint64_t value, int nbits) {
        if (nbits <= 0) return;
        value &= (1ULL << nbits) - 1;
        buf |= (value << bits);
        bits += nbits;
        while (bits >= 8) {
            out.push_back(static_cast<uint8_t>(buf));
            buf >>= 8;
            bits -= 8;
        }
    }

    void flush() {
        if (bits > 0) {
            out.push_back(static_cast<uint8_t>(buf));
            buf = 0;
            bits = 0;
        }
    }
};

struct LSBReader {
    const uint8_t* data;
    size_t size;
    size_t pos{0};
    uint64_t buf{0};
    int buf_bits{0};

    LSBReader(const std::vector<uint8_t>& d)
        : data(d.data()), size(d.size()) {}

    uint64_t readBit() {
        if (buf_bits == 0) {
            if (pos >= size) return 0;
            buf = static_cast<uint64_t>(data[pos++]);
            buf_bits = 8;
        }
        uint64_t bit = buf & 1;
        buf >>= 1;
        buf_bits--;
        return bit;
    }

    uint64_t readBits(int n) {
        if (n <= 0) return 0;
        uint64_t val = 0;
        for (int i = 0; i < n; i++) {
            val |= (readBit() << i);
        }
        return val;
    }

    bool exhausted() const { return pos >= size && buf_bits == 0; }
};

void serializeTreeLSB(LSBWriter& w, compressor::algorithm::HuffmanNode* n, int sym_bits) {
    if (!n) return;
    if (n->isLeaf()) {
        w.writeBits(1, 1);
        w.writeBits(n->symbol, sym_bits);
    } else {
        w.writeBits(0, 1);
        serializeTreeLSB(w, n->left, sym_bits);
        serializeTreeLSB(w, n->right, sym_bits);
    }
}

compressor::algorithm::HuffmanNode* deserializeTreeLSB(LSBReader& r, int sym_bits) {
    uint64_t bit = r.readBit();
    if (bit == 1) {
        uint64_t sym = r.readBits(sym_bits);
        return new compressor::algorithm::HuffmanNode(static_cast<uint16_t>(sym), 0);
    }
    auto* left = deserializeTreeLSB(r, sym_bits);
    auto* right = deserializeTreeLSB(r, sym_bits);
    return new compressor::algorithm::HuffmanNode(left, right);
}

void writeHuffCodeLSB(LSBWriter& w, const compressor::algorithm::HuffmanCode& code) {
    for (uint8_t bit : code.bits) {
        w.writeBit(bit);
    }
}

uint16_t readHuffSymbolLSB(LSBReader& r, compressor::algorithm::HuffmanNode* root) {
    auto* cursor = root;
    while (cursor && !cursor->isLeaf()) {
        uint64_t bit = r.readBit();
        cursor = bit ? cursor->right : cursor->left;
    }
    return cursor ? cursor->symbol : 0;
}

void getLenCode(size_t length, uint16_t& code, uint8_t& extra_bits, uint16_t& extra_val) {
    if (length <= 10) {
        code = static_cast<uint16_t>(257 + length - 3);
        extra_bits = 0;
        extra_val = 0;
    } else if (length <= 18) {
        extra_bits = 1;
        code = static_cast<uint16_t>(265 + (length - 11) / 2);
        extra_val = static_cast<uint16_t>((length - 11) % 2);
    } else if (length <= 34) {
        extra_bits = 2;
        code = static_cast<uint16_t>(269 + (length - 19) / 4);
        extra_val = static_cast<uint16_t>((length - 19) % 4);
    } else if (length <= 66) {
        extra_bits = 3;
        code = static_cast<uint16_t>(273 + (length - 35) / 8);
        extra_val = static_cast<uint16_t>((length - 35) % 8);
    } else if (length <= 130) {
        extra_bits = 4;
        code = static_cast<uint16_t>(277 + (length - 67) / 16);
        extra_val = static_cast<uint16_t>((length - 67) % 16);
    } else if (length <= 257) {
        extra_bits = 5;
        code = static_cast<uint16_t>(281 + (length - 131) / 32);
        extra_val = static_cast<uint16_t>((length - 131) % 32);
    } else if (length == 258) {
        code = 285;
        extra_bits = 0;
        extra_val = 0;
    }
}

void getDistCode(size_t dist, uint8_t& code, uint8_t& extra_bits, uint16_t& extra_val) {
    dist -= 1;
    if (dist < 4) {
        code = static_cast<uint8_t>(dist);
        extra_bits = 0;
        extra_val = 0;
        return;
    }
    uint8_t msb = 0;
    size_t temp = dist >> 2;
    while (temp) { temp >>= 1; msb++; }
    extra_bits = msb;
    code = static_cast<uint8_t>((msb << 1) + 2 + ((dist >> msb) & 1));
    extra_val = static_cast<uint16_t>(dist & ((1 << msb) - 1));
}

std::vector<uint8_t> encode_flate_huffman_old_impl(const std::vector<compressor::algorithm::Triple>& triples) {
    std::vector<uint32_t> lit_freq(kDeflateAlphabet, 0);
    std::vector<uint32_t> dist_freq(kDistanceAlphabet, 0);

    for (const auto& t : triples) {
        if (t.offset == 0) {
            lit_freq[t.literal]++;
        } else {
            uint16_t code;
            uint8_t extra_bits;
            uint16_t extra_val;
            getLenCode(t.length, code, extra_bits, extra_val);
            lit_freq[code]++;

            uint8_t dcode;
            uint8_t dextra;
            uint16_t dval;
            getDistCode(t.offset, dcode, dextra, dval);
            dist_freq[dcode]++;
        }
    }
    lit_freq[256] = 1;

    compressor::algorithm::HuffmanTree lit_tree(lit_freq, kDeflateAlphabet, kDeflateSymbolBits);
    compressor::algorithm::HuffmanTree dist_tree(dist_freq, kDistanceAlphabet, kDistanceSymbolBits);
    auto lit_dict = lit_tree.buildDictionary();
    auto dist_dict = dist_tree.buildDictionary();

    std::vector<uint8_t> result;
    LSBWriter w{result};

    w.writeBits(0x46, 8);
    serializeTreeLSB(w, lit_tree.getRoot(), kDeflateSymbolBits);
    serializeTreeLSB(w, dist_tree.getRoot(), kDistanceSymbolBits);

    for (const auto& t : triples) {
        if (t.offset == 0) {
            writeHuffCodeLSB(w, lit_dict[t.literal]);
        } else {
            uint16_t len_code;
            uint8_t len_extra_bits;
            uint16_t len_extra_val;
            getLenCode(t.length, len_code, len_extra_bits, len_extra_val);
            writeHuffCodeLSB(w, lit_dict[len_code]);
            for (int i = 0; i < len_extra_bits; i++) {
                w.writeBit((len_extra_val >> i) & 1);
            }

            uint8_t dcode;
            uint8_t dextra;
            uint16_t dval;
            getDistCode(t.offset, dcode, dextra, dval);
            writeHuffCodeLSB(w, dist_dict[dcode]);
            for (int i = 0; i < dextra; i++) {
                w.writeBit((dval >> i) & 1);
            }
        }
    }

    writeHuffCodeLSB(w, lit_dict[256]);
    w.flush();
    return result;
}

std::vector<uint8_t> decode_flate_huffman_old_impl(const std::vector<uint8_t>& compressed) {
    LSBReader r(compressed);
    std::vector<uint8_t> result;

    uint64_t header = r.readBits(8);
    (void)header;

    auto* lit_root = deserializeTreeLSB(r, kDeflateSymbolBits);
    if (!lit_root) return result;
    auto* dist_root = deserializeTreeLSB(r, kDistanceSymbolBits);
    if (!dist_root) { compressor::algorithm::destroyHuffmanNode(lit_root); return result; }

    for (;;) {
        uint16_t sym = readHuffSymbolLSB(r, lit_root);

        if (sym == 256) break;

        if (sym < 256) {
            result.push_back(static_cast<uint8_t>(sym));
        } else {
            size_t len_idx = sym - 257;
            uint32_t length = kLengthBases[len_idx];
            uint8_t lextra = kLengthExtra[len_idx];
            if (lextra > 0) {
                uint64_t extra = r.readBits(lextra);
                length += static_cast<uint32_t>(extra);
            }

            uint16_t dsym = readHuffSymbolLSB(r, dist_root);
            uint32_t dist = kDistBases[dsym];
            uint8_t dextra = kDistExtra[dsym];
            if (dextra > 0) {
                uint64_t extra = r.readBits(dextra);
                dist += static_cast<uint32_t>(extra);
            }

            size_t copy_start = result.size() > dist ? result.size() - dist : 0;
            for (uint32_t i = 0; i < length; ++i) {
                if (copy_start + i < result.size()) {
                    result.push_back(result[copy_start + i]);
                }
            }
        }
    }

    compressor::algorithm::destroyHuffmanNode(lit_root);
    compressor::algorithm::destroyHuffmanNode(dist_root);
    return result;
}

std::vector<uint8_t> encode_3hm_huffman_old_impl(const std::vector<compressor::algorithm::Triple>& triples,
                                                   const compressor::algorithm::HuffmanTree3HMConfig& cfg) {
    using namespace compressor::algorithm;

    const uint32_t cb = 8;
    const uint32_t mask = (1u << cb) - 1;
    const uint32_t off_dict_size = 1u << cb;
    const uint32_t len_dict_size = 1u << cb;
    const uint32_t max_off_bits = cfg.max_offset_bits;
    const uint32_t max_len_bits = cfg.max_length_bits;
    const uint32_t oc = (max_off_bits + cb - 1) / cb;
    const uint32_t lc = (max_len_bits + cb - 1) / cb;

    std::vector<uint32_t> lit_freq(256, 0);
    std::vector<uint32_t> off_freq(off_dict_size, 0);
    std::vector<uint32_t> len_freq(len_dict_size, 0);

    auto count_chunks = [&](uint32_t value, std::vector<uint32_t>& freq) {
        for (uint32_t i = 0; i < oc; ++i) {
            uint32_t chunk = (value >> (i * cb)) & mask;
            freq[chunk]++;
        }
    };
    auto count_len_chunks = [&](uint32_t value, std::vector<uint32_t>& freq) {
        for (uint32_t i = 0; i < lc; ++i) {
            uint32_t chunk = (value >> (i * cb)) & mask;
            freq[chunk]++;
        }
    };

    std::vector<uint8_t> run_buf;
    auto flush_run = [&]() {
        if (run_buf.empty()) return;
        uint32_t run_len = static_cast<uint32_t>(run_buf.size());
        count_chunks(0, off_freq);
        count_len_chunks(run_len, len_freq);
        for (uint8_t lit : run_buf) lit_freq[lit]++;
        run_buf.clear();
    };

    for (const auto& t : triples) {
        if (t.offset == 0) {
            run_buf.push_back(t.literal);
        } else {
            flush_run();
            count_chunks(t.offset, off_freq);
            count_len_chunks(t.length, len_freq);
        }
    }
    flush_run();
    count_chunks(0, off_freq);
    count_len_chunks(0, len_freq);

    for (size_t i = 0; i < off_dict_size; ++i) {
        if (off_freq[i] == 0) off_freq[i] = 1;
    }
    for (size_t i = 0; i < len_dict_size; ++i) {
        if (len_freq[i] == 0) len_freq[i] = 1;
    }

    HuffmanTree lit_tree(lit_freq, 256, 8);
    HuffmanTree off_tree(off_freq, off_dict_size, cb);
    HuffmanTree len_tree(len_freq, len_dict_size, cb);

    auto lit_dict = lit_tree.buildDictionary();
    auto off_dict = off_tree.buildDictionary();
    auto len_dict = len_tree.buildDictionary();

    std::vector<uint8_t> out;
    LSBWriter w{out};

    w.writeBits(0x33, 8);
    w.writeBits(static_cast<uint64_t>(max_off_bits), 8);
    w.writeBits(static_cast<uint64_t>(max_len_bits), 8);
    w.writeBits(static_cast<uint64_t>(cb), 8);
    w.writeBits(static_cast<uint64_t>(cb), 8);

    serializeTreeLSB(w, lit_tree.getRoot(), 8);
    serializeTreeLSB(w, off_tree.getRoot(), static_cast<int>(cb));
    serializeTreeLSB(w, len_tree.getRoot(), static_cast<int>(cb));

    run_buf.clear();
    for (const auto& t : triples) {
        if (t.offset == 0) {
            run_buf.push_back(t.literal);
        } else {
            if (!run_buf.empty()) {
                for (uint32_t i = 0; i < oc; ++i) {
                    uint32_t chunk = (0 >> (i * cb)) & mask;
                    writeHuffCodeLSB(w, off_dict[chunk]);
                }
                uint32_t run_len = static_cast<uint32_t>(run_buf.size());
                for (uint32_t i = 0; i < lc; ++i) {
                    uint32_t chunk = (run_len >> (i * cb)) & mask;
                    writeHuffCodeLSB(w, len_dict[chunk]);
                }
                for (uint8_t lit : run_buf) writeHuffCodeLSB(w, lit_dict[lit]);
                run_buf.clear();
            }
            for (uint32_t i = 0; i < oc; ++i) {
                uint32_t chunk = (t.offset >> (i * cb)) & mask;
                writeHuffCodeLSB(w, off_dict[chunk]);
            }
            for (uint32_t i = 0; i < lc; ++i) {
                uint32_t chunk = (t.length >> (i * cb)) & mask;
                writeHuffCodeLSB(w, len_dict[chunk]);
            }
        }
    }
    if (!run_buf.empty()) {
        for (uint32_t i = 0; i < oc; ++i) {
            uint32_t chunk = (0 >> (i * cb)) & mask;
            writeHuffCodeLSB(w, off_dict[chunk]);
        }
        uint32_t run_len = static_cast<uint32_t>(run_buf.size());
        for (uint32_t i = 0; i < lc; ++i) {
            uint32_t chunk = (run_len >> (i * cb)) & mask;
            writeHuffCodeLSB(w, len_dict[chunk]);
        }
        for (uint8_t lit : run_buf) writeHuffCodeLSB(w, lit_dict[lit]);
        run_buf.clear();
    }

    w.flush();
    return out;
}

std::vector<uint8_t> decode_3hm_huffman_old_impl(const std::vector<uint8_t>& compressed) {
    using namespace compressor::algorithm;

    LSBReader r(compressed);

    uint64_t marker = r.readBits(8);
    (void)marker;

    uint64_t ob_val = r.readBits(8);
    uint64_t lb_val = r.readBits(8);
    uint64_t ocb_val = r.readBits(8);
    uint64_t lcb_val = r.readBits(8);

    uint32_t max_off_bits = static_cast<uint32_t>(ob_val);
    uint32_t max_len_bits = static_cast<uint32_t>(lb_val);
    uint32_t off_chunk_bits = static_cast<uint32_t>(ocb_val);
    uint32_t len_chunk_bits = static_cast<uint32_t>(lcb_val);

    uint32_t oc = (max_off_bits + off_chunk_bits - 1) / off_chunk_bits;
    uint32_t lc = (max_len_bits + len_chunk_bits - 1) / len_chunk_bits;
    uint32_t mask = (1u << off_chunk_bits) - 1;
    uint32_t cb = off_chunk_bits;

    auto* lit_root = deserializeTreeLSB(r, 8);
    auto* off_root = deserializeTreeLSB(r, static_cast<int>(off_chunk_bits));
    auto* len_root = deserializeTreeLSB(r, static_cast<int>(len_chunk_bits));

    std::vector<uint8_t> result;

    for (;;) {
        if (r.exhausted()) break;

        uint32_t offset_v = 0;
        for (uint32_t i = 0; i < oc; ++i) {
            uint16_t sym = readHuffSymbolLSB(r, off_root);
            offset_v |= (static_cast<uint32_t>(sym) << (i * cb));
        }

        uint32_t len_v = 0;
        for (uint32_t i = 0; i < lc; ++i) {
            uint16_t sym = readHuffSymbolLSB(r, len_root);
            len_v |= (static_cast<uint32_t>(sym) << (i * cb));
        }

        if (offset_v == 0 && len_v == 0) break;

        if (offset_v == 0) {
            for (uint32_t i = 0; i < len_v; ++i) {
                uint16_t sym = readHuffSymbolLSB(r, lit_root);
                result.push_back(static_cast<uint8_t>(sym));
            }
        } else {
            size_t copy_start = result.size() >= offset_v ? result.size() - offset_v : 0;
            for (uint32_t i = 0; i < len_v; ++i) {
                result.push_back(result[copy_start + (i % offset_v)]);
            }
        }
    }

    compressor::algorithm::destroyHuffmanNode(lit_root);
    compressor::algorithm::destroyHuffmanNode(off_root);
    compressor::algorithm::destroyHuffmanNode(len_root);
    return result;
}

}  // namespace

namespace compressor::algorithm {

std::vector<uint8_t> encode_flate_huffman(const std::vector<Triple>& triples) {
    return encode_flate_huffman_old_impl(triples);
}

std::vector<uint8_t> decode_flate_huffman(const std::vector<uint8_t>& compressed) {
    return decode_flate_huffman_old_impl(compressed);
}

std::vector<uint8_t> encode_3hm_huffman(const std::vector<Triple>& triples,
                                          const HuffmanTree3HMConfig& cfg) {
    return encode_3hm_huffman_old_impl(triples, cfg);
}

std::vector<uint8_t> decode_3hm_huffman(const std::vector<uint8_t>& compressed) {
    return decode_3hm_huffman_old_impl(compressed);
}

DPFlateNonStreamingResult compress_bytes_dpflate(
    const std::vector<uint8_t>& input,
    const DPFlateConfig& config) {
    DPFlateNonStreamingResult result;
    if (input.empty()) return result;
    if (core_new::is_streaming_cancel_requested()) throw std::runtime_error("cancelled");

    std::vector<models::DPNode> dp_nodes(input.size() + 1,
        models::DPNode(0, 0, -2));
    dp_nodes[0] = models::DPNode(0, 0, -1, Triple(0, 0, 0));

    LZDP lzdp(config.lzdp);
    VectorByteInput view{input, 0};
    lzdp.dpforward(view, dp_nodes, 0, input.size());

    if (core_new::is_streaming_cancel_requested()) throw std::runtime_error("cancelled");

    int cur_pos = 0;
    std::vector<Triple> triples = lzdp.dpbacktrack(dp_nodes, cur_pos, 0);

    result.triples = triples;

    if (config.use_3hfmtree) {
        result.compressed = encode_3hm_huffman(triples, config.huffman_3hm);
    } else {
        result.compressed = encode_flate_huffman(triples);
    }

    return result;
}

std::vector<uint8_t> decompress_bytes_dpflate(
    const std::vector<uint8_t>& compressed,
    const DPFlateConfig& config) {
    if (core_new::is_streaming_cancel_requested()) throw std::runtime_error("cancelled");
    if (config.use_3hfmtree) {
        return decode_3hm_huffman(compressed);
    }
    return decode_flate_huffman(compressed);
}

}  // namespace compressor::algorithm

namespace compressor::algorithm::pipeline {

namespace fs = std::filesystem;

DPFlateStreamingPipeline::DPFlateStreamingPipeline(DPFlateConfig config, DPFlateStreamingOptions opts)
    : config_(std::move(config)), opts_(std::move(opts)) {}

std::string DPFlateStreamingPipeline::temp_a_path() const {
    return (fs::path(opts_.workspace_dir) / opts_.temp_a_name).string();
}

std::string DPFlateStreamingPipeline::temp_b_path() const {
    return (fs::path(opts_.workspace_dir) / opts_.temp_b_name).string();
}

void DPFlateStreamingPipeline::compress_file(const std::string& input_path, const std::string& output_path) {
    fprintf(stderr, "[DPFLATE-STREAM] compress_file start: %s\n", input_path.c_str());
    fflush(stderr);
    fs::create_directories(opts_.workspace_dir);
    std::error_code ec;
    fs::remove(temp_a_path(), ec);
    fs::remove(temp_b_path(), ec);

    if (core_new::is_streaming_cancel_requested()) throw std::runtime_error("cancelled");

    LZDP lzdp(config_.lzdp);
    fprintf(stderr, "[DPFLATE-STREAM] phase1 start...\n");
    fflush(stderr);
    const Phase1Result phase1 = run_phase1_dpforward(lzdp, config_.lzdp, input_path, temp_a_path(), opts_.chunk_size);
    fprintf(stderr, "[DPFLATE-STREAM] phase1 done: total_input=%zu\n", phase1.total_input_bytes);
    fflush(stderr);

    if (core_new::is_streaming_cancel_requested()) throw std::runtime_error("cancelled");

    fprintf(stderr, "[DPFLATE-STREAM] phase2 start...\n");
    fflush(stderr);
    const Phase2Result phase2 = run_phase2_dpbacktrack(lzdp, phase1, temp_a_path(), temp_b_path(), opts_.chunk_size);
    fprintf(stderr, "[DPFLATE-STREAM] phase2 done: n_triples=%zu\n", phase2.triples.size());
    fflush(stderr);

    if (core_new::is_streaming_cancel_requested()) throw std::runtime_error("cancelled");

    auto tr = phase2.triples;

    compressor::utils::_buffer pending;
    std::vector<uint8_t> encoded;
    if (config_.use_3hfmtree) {
        encoded = encode_3hm_huffman(tr, config_.huffman_3hm);
    } else {
        encoded = encode_flate_huffman(tr);
    }

    fprintf(stderr, "[DPFLATE-STREAM] encoding done: compressed=%zu writing file...\n", encoded.size());
    fflush(stderr);

    {
        std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("DPFlateStreamingPipeline: cannot open " + output_path);
        if (!encoded.empty()) {
            out.write(reinterpret_cast<const char*>(encoded.data()), static_cast<std::streamsize>(encoded.size()));
        }
    }
    fprintf(stderr, "[DPFLATE-STREAM] compress_file done.\n");
    fflush(stderr);
}

}  // namespace compressor::algorithm::pipeline
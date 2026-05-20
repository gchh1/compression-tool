#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <variant>
#include <vector>

namespace compressor::viz {

// ── Event types ───────────────────────────────────────────

struct MatchEvent {
    uint32_t input_pos;       // absolute byte position in original data
    uint16_t offset;          // 0 = literal, >0 = match distance back
    uint16_t length;          // match length (offset>0) or 0 (literal)
    uint8_t  literal;         // literal byte value (offset==0)
};

struct BlockBoundary {
    uint32_t block_index;
    uint32_t input_start;     // first byte of this block in original data
    uint32_t input_bytes;     // original bytes covered by this block
    uint32_t literal_count;
    uint32_t match_count;
    uint32_t output_bytes;    // compressed size of this block
};

struct HuffmanTreeBuilt {
    uint32_t block_index;
    uint8_t  tree_type;       // 0=lit/len, 1=dist (Deflate); 0-3 (Brotli)
    uint16_t alphabet_size;
    uint8_t  code_lengths[286];
};

struct DPStateEvent {
    uint32_t position;
    uint32_t token_count;
    uint32_t predecessor;
    uint16_t match_offset;
    uint16_t match_length;
    uint8_t  is_chosen;       // 1 = on optimal path
    uint8_t  reachable;       // 1 = this position is reachable
};

struct DPCandidateEvent {
    uint32_t position;        // absolute byte position in original data
    uint16_t offset;          // match distance (0 = literal)
    uint16_t length;          // match length (offset>0) or 0 (literal)
    uint8_t  literal;         // literal byte value (offset==0)
    uint8_t  is_chosen;       // 1 = this candidate is on the optimal path
};

using VizEvent = std::variant<MatchEvent, BlockBoundary,
                               HuffmanTreeBuilt, DPStateEvent,
                               DPCandidateEvent>;

// ── Observer interface ────────────────────────────────────

class IVizObserver {
public:
    virtual ~IVizObserver() = default;

    virtual void onEvent(VizEvent event) = 0;
    virtual void onBlockFinish() = 0;
    virtual void onCompressionFinish() = 0;
};

// ── Event serialization helpers ───────────────────────────

/// Serialize event to byte buffer, returns bytes written.
/// Buffer must be at least 320 bytes.
inline size_t serializeEvent(const VizEvent& event, uint8_t* out) {
    return std::visit([out](const auto& e) -> size_t {
        using T = std::decay_t<decltype(e)>;

        if constexpr (std::is_same_v<T, MatchEvent>) {
            out[0] = 0;  // type tag
            size_t off = 1;
            memcpy(out + off, &e.input_pos, 4);  off += 4;
            memcpy(out + off, &e.offset, 2);      off += 2;
            memcpy(out + off, &e.length, 2);      off += 2;
            out[off] = e.literal;                  off += 1;
            return off;  // 10 bytes
        }
        else if constexpr (std::is_same_v<T, BlockBoundary>) {
            out[0] = 1;
            size_t off = 1;
            memcpy(out + off, &e.block_index, 4);   off += 4;
            memcpy(out + off, &e.input_start, 4);    off += 4;
            memcpy(out + off, &e.input_bytes, 4);    off += 4;
            memcpy(out + off, &e.literal_count, 4);  off += 4;
            memcpy(out + off, &e.match_count, 4);    off += 4;
            memcpy(out + off, &e.output_bytes, 4);   off += 4;
            return off;  // 25 bytes
        }
        else if constexpr (std::is_same_v<T, HuffmanTreeBuilt>) {
            out[0] = 2;
            size_t off = 1;
            memcpy(out + off, &e.block_index, 4);   off += 4;
            out[off] = e.tree_type;                   off += 1;
            memcpy(out + off, &e.alphabet_size, 2);  off += 2;
            memcpy(out + off, e.code_lengths, e.alphabet_size);  off += e.alphabet_size;
            return off;
        }
        else if constexpr (std::is_same_v<T, DPStateEvent>) {
            out[0] = 3;
            size_t off = 1;
            memcpy(out + off, &e.position, 4);       off += 4;
            memcpy(out + off, &e.token_count, 4);    off += 4;
            memcpy(out + off, &e.predecessor, 4);    off += 4;
            memcpy(out + off, &e.match_offset, 2);   off += 2;
            memcpy(out + off, &e.match_length, 2);   off += 2;
            out[off] = e.is_chosen;                   off += 1;
            out[off] = e.reachable;                   off += 1;
            return off;  // 19 bytes
        }
        else if constexpr (std::is_same_v<T, DPCandidateEvent>) {
            out[0] = 4;
            size_t off = 1;
            memcpy(out + off, &e.position, 4);       off += 4;
            memcpy(out + off, &e.offset, 2);          off += 2;
            memcpy(out + off, &e.length, 2);          off += 2;
            out[off] = e.literal;                      off += 1;
            out[off] = e.is_chosen;                    off += 1;
            return off;  // 11 bytes
        }
    }, event);
}

/// Serialize event *payload* (no type-tag byte, for section-based .viz v2).
inline size_t serializeEventPayload(const VizEvent& event, uint8_t* out) {
    return std::visit([out](const auto& e) -> size_t {
        using T = std::decay_t<decltype(e)>;

        if constexpr (std::is_same_v<T, MatchEvent>) {
            size_t off = 0;
            memcpy(out + off, &e.input_pos, 4);  off += 4;
            memcpy(out + off, &e.offset, 2);      off += 2;
            memcpy(out + off, &e.length, 2);      off += 2;
            out[off] = e.literal;                  off += 1;
            return off;  // 9 bytes
        }
        else if constexpr (std::is_same_v<T, BlockBoundary>) {
            size_t off = 0;
            memcpy(out + off, &e.block_index, 4);   off += 4;
            memcpy(out + off, &e.input_start, 4);    off += 4;
            memcpy(out + off, &e.input_bytes, 4);    off += 4;
            memcpy(out + off, &e.literal_count, 4);  off += 4;
            memcpy(out + off, &e.match_count, 4);    off += 4;
            memcpy(out + off, &e.output_bytes, 4);   off += 4;
            return off;  // 24 bytes
        }
        else if constexpr (std::is_same_v<T, HuffmanTreeBuilt>) {
            size_t off = 0;
            memcpy(out + off, &e.block_index, 4);   off += 4;
            out[off] = e.tree_type;                   off += 1;
            memcpy(out + off, &e.alphabet_size, 2);  off += 2;
            memcpy(out + off, e.code_lengths, e.alphabet_size);  off += e.alphabet_size;
            return off;
        }
        else if constexpr (std::is_same_v<T, DPStateEvent>) {
            size_t off = 0;
            memcpy(out + off, &e.position, 4);       off += 4;
            memcpy(out + off, &e.token_count, 4);    off += 4;
            memcpy(out + off, &e.predecessor, 4);    off += 4;
            memcpy(out + off, &e.match_offset, 2);   off += 2;
            memcpy(out + off, &e.match_length, 2);   off += 2;
            out[off] = e.is_chosen;                   off += 1;
            out[off] = e.reachable;                   off += 1;
            return off;  // 18 bytes
        }
        else if constexpr (std::is_same_v<T, DPCandidateEvent>) {
            size_t off = 0;
            memcpy(out + off, &e.position, 4);       off += 4;
            memcpy(out + off, &e.offset, 2);          off += 2;
            memcpy(out + off, &e.length, 2);          off += 2;
            out[off] = e.literal;                      off += 1;
            out[off] = e.is_chosen;                    off += 1;
            return off;  // 10 bytes
        }
    }, event);
}

}  // namespace compressor::viz

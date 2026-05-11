# Algorithm Layer

This directory contains pure compression algorithm implementations and basic encoding/decoding primitives.

## Responsibilities
- Pure data transformations (compression/decompression) without high-level semantics.
- No dependency on higher-level layers (like `core` or `api`).
- Focuses on raw buffer processing.

## Naming Dictionary
- `LZSS`
- `LZDP` (Historically referred to as LZMine in some places, now normalized to LZDP)
- `Deflate`
- `DPFlate` (Historically MyFlate, now DPFlate)
- `Brotli`
- `Zstd`
- `Huffman`
- `Delta`

## Restrictions
- Should not include stateful compressor instances that wrap these primitives (those belong in `core/`).

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build commands

```bash
# Default build (Python bindings enabled, WASM disabled)
cmake -B build -G Ninja -DCMAKE_CXX_COMPILER="D:/Program Files/llvm/bin/clang++.exe"
cmake --build build

# Release build (add -DCMAKE_BUILD_TYPE=Release)

# WebAssembly build (requires emsdk)
emcmake cmake -B build_wasm -DBUILD_WASM=ON
make -C build_wasm

# Python bindings build (requires pybind11 in third_party/)
cmake -B build_pybind -DBUILD_PYTHON=ON
make -C build_pybind

# Build and run the one test
cmake --build build --target test_Deflate
./build/tests/test_Deflate
```

- Use Ninja as the generator on Windows.
- The compiler toolchain is LLVM Clang (see `.vscode/scan-kit.json`).
- Clang-format config: Google-based, 4-space indent, 80-column limit.

## Architecture

The library `<algorithm>` defines the streaming compression interface. Every algorithm derives from `AlgorithmBase` (in `src/algorithm/include/IAlgorithm.hpp`), which bridges the virtual `handle()` to `BitReader`/`BitWriter` (both in `src/utils/include/`). The central type is:

```
AlgorithmStatus { bytes_consumed, bytes_produced, need_input, need_output, done }
```

Algorithms operate in push-pull style and may return `need_input`/`need_output` when their I/O windows are exhausted.

**Key algorithms:**
- `Deflate` — LZ77 with hash-chain matching + Huffman coding, driven by a 3-state machine (`FIND_MATCHES` → `BUILD_TREE` → `FLUSH_TOKENS`).
- `Inflate` — Reverses Deflate; also a state machine (`READ_BLOCK_HEADER` → `READ_TREE` → `DECODE_TOKENS`).
- `DeltaEncode` / `DeltaDecode` — Simple delta coding.
- `HuffmanTree` — Builds canonical Huffman trees from frequency maps and handles serialization.
- `LZ77`, `LZSS` — Source files exist but are not compiled in the current `algorithm` CMake target.

**Pipeline layer (`src/processor/`):**
- `StreamProcessor` wraps one `IAlgorithm` with a `RingBuffer` as input and a `std::vector<uint8_t>` as output. It loops calling `algo->process()` until the algorithm asks for more input, more output space, or signals done.
- `Pipeline` chains two `StreamProcessor`s (preprocessor → compressor). `drain()` pumps output from one stage into the next. When only one algorithm is needed the second stage can be null.

**Archiver layer (`src/archiver/`):**
- `PackWriter` — Creates a multi-file archive. `beginFile()` writes a serialized `EntryHeader` (filepath, algorithm IDs, sizes), creates a `Pipeline` for that file, then `pushFileData()` feeds data through. `endFile()` patches the compressed size back into the header.
- `PackReader` — Parses headers via `IDataReader` into a `vector<EntryHeader>`, then `extractStream()` builds a decompression Pipeline per entry.
- `EntryHeader` — Self-serializing struct with `serialize()` / `deserialize()` methods.

**Core (`src/core/`):**
- `AlgorithmFactory::createAlgorithm(AlgorithmID)` — Constructs concrete `IAlgorithm` instances from an enum (`None`, `Deflate`, `Inflate`, `DeltaEncode`, `DeltaDecode`).
- `getDecompressorID()` / `getPostpressorID()` — Map compressor IDs to their inverse (e.g. `Deflate` → `Inflate`).

**Preprocessor (`src/preprocessor/`):**
- `IPreprocessor` interface with `encode()` / `decode()`.
- `ImagePreprocessor` — Wraps delta coding for image data.

**Bindings (`src/bindings/`):**
- `pybind/` — Exposes the compression API to Python via Pybind11.
- `wasm/` — WebAssembly bindings for browser use.

**Namespace:** All code lives under `compressor::` (with sub-namespaces `algorithm`, `core`, `processor`, `archiver`, `utils`).

## Notes

- The `src/bindings/pybind/pybind_module.cpp` references older API types (`ICompressor`, `DeflateCompressor`, `LZSSCompressor`, `Archiver`) that do not exist in the current source tree — the bindings are out of sync and need updating to use the current `Pipeline`/`PackWriter`/`PackReader` API.
- C++20 is required (`CMAKE_CXX_STANDARD 20`).

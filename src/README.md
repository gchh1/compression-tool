# Source Tree Architecture

This file defines the intended ownership of `src/`.

## Core Modules

- `algorithm/` - Compression algorithm implementations.
- `core/` - Compressor adapters/factory over algorithms.
- `processor/` - Pipeline and streaming process orchestration.
- `archiver/` - Pack/unpack archive read/write layer.
- `api/` - Public C++ API composition layer.
- `bindings/` - Language bindings (pybind/wasm).
- `ade/` - Algorithm Decision Engine and feature extraction (C++: flat `ade/include/*.hpp`; `ade/runtime/*.cpp`; see `src/ade/README.md`).
- `gui/` - Desktop GUI application layer.
  - `gui/models.py` - records, enums, algorithm metadata shared by UI and engine.
  - `gui/engine/` - C++ bridge, `CompressionEngine`, `file_protocol`, `token_parser`.
  - `gui/config/` - persisted settings and `ThemeManager`.
  - `gui/ade/` - ADE Python: `DecisionEngine`, features, training store, explorer, params.
  - `gui/ui/` - PyQt6 shell: `main_window`, `worker`, `table`, `views/`, `panels/`, `dialogs/`, `helpers.py`.
  - `gui/windows/` - HTML report / standalone window generators (heatmap, comparison, network sim, etc.).
  - `gui/utils/` - logging setup, resources, `file_helper` (scan / batch load).
  - `gui/algorithms/` - optional pure-Python compressors (e.g. transformer).
- `utils/` - Shared infrastructure (bit I/O, ring buffer, **`DataChunk` / `MemoryPool`** for streaming; headers under `utils/include/`).

## Non-Module Directories

- `logs/` under `src/` is **not** a valid source module location.
  - Runtime logs belong to repository root `logs/` (dev) or `Package/logs/` (packaged app).
- Experimental scripts should not live in `src/`.
  - Use `scripts/experiments/`.
- Test compressed samples should not live in `src/`.
  - Use `resources/fixtures/compressed/`.
- Runtime DLLs should not be committed under `src/`.
  - Build/runtime binaries belong to `build/` (dev) and `Package/bin/` (distribution).

## Naming and Boundaries

- Keep module names domain-specific (avoid ambiguous names like `others/`).
- New source modules must be referenced from `src/CMakeLists.txt` (if C++) or from package imports (if Python).


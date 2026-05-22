# ADE module ownership

`src/ade` only contains ADE C++ code and local build entrypoints.

## Directory layout

- **`include/`** — **flat** public `.hpp` only (no `include/features/` etc.); sibling to `bench/`, `features/`, `models/`, `runtime/`, `tools/`. CMake exposes this single directory; includes stay `#include "FeatureExtractorV3.hpp"` etc.

- **`features/`**, **`models/`** — optional future **`.cpp`** implementations (no `*/src/`, no nested `include/`).

- **`runtime/`** — **`.cpp`** (`ADEBridge.cpp`, `DecisionEngine.cpp`) sibling to `include/`.

- **`tools/`**, **`bench/`** — executables.

## Implementation vs headers

Many types are still **header-heavy**. When splitting to `.cpp`, put declarations in `include/*.hpp` and definitions in `runtime/` or domain-root `.cpp` as documented in `docs/design/algorithm_decision_engine_design.md`.

## Out-of-scope paths

- Training/model assets: `assets/ade/`
- C++ tests: `tests/cpp/ade/`

## Include rules

Link target `ade` and use flat names from `src/ade/include/`:

- `#include "FeatureExtractorV3.hpp"`
- `#include "DecisionEngine.hpp"`
- `#include "RandomForest.hpp"`

Do not add subfolders under `include/` unless filenames would collide (avoid duplicate basenames).

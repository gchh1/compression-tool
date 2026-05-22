# Bindings Layer

This directory contains language-specific glue code and adapters.

## Responsibilities
- Exposes the C++ capabilities (via `api/` layer) to other languages (e.g., Python via Pybind11).
- Maps target language data structures to C++ primitives.
- No business or orchestration logic should exist here.

## Structure
- `pybind/`: Python bindings for desktop/CLI usage.

## Restrictions
- Should strictly wrap the `api/` facade rather than bypassing it to directly call `algorithm/` or `core/` unless strictly necessary for specific feature exposure that is not suited for the general API.

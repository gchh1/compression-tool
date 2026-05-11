# Core Layer

This directory contains compressor semantics, parameter mapping, factory patterns, and strategy orchestration.

## Responsibilities
- Wraps raw algorithms from the `algorithm/` layer into stateful `*Compressor` classes.
- Standardizes parameter setters/getters (e.g., `set_dp_top`, `get_dp_top`).
- Exposes `AlgorithmFactory` to instantiate compressors by type.
- Serves as the domain layer for compression logic and object lifecycle.

## Naming Dictionary
- Compressor classes should end with `Compressor` (e.g., `LZDPCompressor`, `DPFlateCompressor`).
- Parameter names:
  - `dp_top` (was `dp_depth` for LZDP)
  - `dp_sub_match_max` (was `dp_depth` for DPFlate)

## Restrictions
- Must not contain pybind/wasm logic.
- Must not manage multi-file packing/unpacking (which belongs to `archiver/` and `processor/`).

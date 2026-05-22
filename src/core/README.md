# Core Layer

This directory contains algorithm parameter structures, factory `createAlgorithm()`, and strategy orchestration.

## Responsibilities
- Defines parameter structs (`LzdpWholeFileParams`, `DpflatePipelineParams`, `DeflatePipelineParams`) for algorithm construction.
- `AlgorithmFactory` maps `AlgorithmID` → `algorithm::IAlgorithm` instances.
- `AlgorithmFactory.cpp` owns the single-point per-algorithm configuration mapping (knobs → `algorithm::*` ctor).

## Naming Dictionary
- Algorithm implementations live in `algorithm/` (e.g., `LZDP_OutOfCore`, `DPFlate`, `Deflate`).
- Parameter names:
  - `dp_top` (was `dp_depth` for LZDP)
  - `dp_sub_match_max` (was `dp_depth` for DPFlate)

## Restrictions
- Must not contain pybind/wasm logic.
- Must not manage multi-file packing/unpacking (which belongs to `archiver/` and `processor/`).

# Archiver Layer

`archiver/` is the container layer for packed file metadata and payload streams.

## Responsibilities

- Read/write archive entry headers and payload boundaries.
- Expose file-oriented APIs (`PackReader`, `PackWriter`) over container format.
- Coordinate with `processor/` to run pipelines, without owning pipeline construction logic.

## Non-Responsibilities

- No direct algorithm-chain construction rules (moved to `processor/PipelineBuilder`).
- No algorithm primitive implementation.

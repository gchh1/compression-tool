# Processor Layer

`processor/` is responsible for stream orchestration and algorithm-chain execution.

## Responsibilities

- Build processing pipelines from algorithm IDs.
- Execute chunked push/pull processing via `Pipeline` and `StreamProcessor`.
- Provide chain builders (`PipelineBuilder`) for compression/decompression orchestration.

## Non-Responsibilities

- No archive container parsing/writing (belongs to `archiver/`).
- No algorithm primitive implementation (belongs to `algorithm/`).

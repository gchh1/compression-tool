# Utils Layer

`utils/` stores low-level reusable utilities shared by algorithm components.

## Current Scope

- Bit-level IO primitives:
  - `BitReader.hpp`
  - `BitWriter.hpp`
- Generic data structure utility:
  - `RingBuffer.hpp`
- Streaming buffer helpers (namespace `compressor::memory`, kept for API stability):
  - `DataChunk.hpp`
  - `MemoryPool.hpp`

## Rule

Keep this layer dependency-light and free of business semantics.

# API Layer

This directory is the stable C++ API facade for the entire compression tool.

## Responsibilities
- Serves as the single, stable entry point for C++ clients, CLI, and bindings.
- Aggregates capabilities from `archiver`, `processor`, `core`, and `memory`.
- Isolates clients from internal implementation changes and refactoring.

## Restrictions
- Must not contain language-specific binding details (like Pybind11 attributes).
- Should keep headers clean and minimal.

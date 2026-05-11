# Contributing Guide

This repository follows the governance defined in `docs/engineering_standardization_directive.md`.

## 1) Branch and commit policy

- Keep commits reviewable and scoped (no mixed "format only + behavior change").
- Commit message must include:
  - what changed
  - why it changed
- Large refactors must be split by module (`algorithm`, `core`, `bindings`, `gui`).

## 2) Layering and dependency direction

- Preferred one-way direction:
  - entry/app (`gui/main.py`, app wiring)
  - service/domain (`gui.engine`, `gui.config`, `gui.ade`, `gui.models` — orchestration & types)
  - algorithm/infrastructure (`src/core`, `src/algorithm`)
- Forbidden:
  - algorithm layer depending on GUI modules.

## 3) Naming rules (current baseline)

- C++:
  - types: `PascalCase`
  - methods/functions: `snake_case` or project-established style within same class (do not mix in one type)
- Python:
  - modules/functions/variables: `snake_case`
  - classes: `PascalCase`
  - constants: `UPPER_SNAKE_CASE`
- Config keys:
  - use `snake_case`
  - keep one meaning per key across algorithms.

## 4) Build and packaging baseline

- Build commands: see `docs/standardization/build-baseline.md` (index: `docs/standardization/README.md`).
- Build outputs should remain in build tree (`build/...`) or package output directories, not source dirs.

## 5) Artifacts and generated files

- Do not commit runtime logs, temporary outputs, generated compression samples, or local binaries.
- If a generated file must be versioned for reproducibility, document why in the PR.
- Follow file ownership rules in `docs/standardization/file-ownership-by-layer.md`.
- Do not place temporary scripts/results in repository root.

## 6) Governance gates

- Before high-risk changes (path moves, API semantic changes, uncertain deletions), update and get approval on:
  - `docs/standardization/risk-decisions.md`
- Keep stage artifacts updated:
  - technical debt report
  - path mapping table
  - naming dictionary
  - duplicate-code cleanup list


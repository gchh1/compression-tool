# `gui.ui` ownership map

This directory (`src/gui/ui/`) is the desktop GUI presentation layer (historically `gui.widgets`).

## Ownership by responsibility

- **Window composition / app shell**
  - `main_window.py` (main application window, workflow orchestration)

- **Modal dialogs (LZ/Flate demos, block heatmap, comparison, network sim, heatmap, Huffman)**
  - `dialogs/*.py` — implementations
  - `visualization_windows.py` — optional full barrel re-export (compat); prefer `dialogs/…` in new code

- **View components (page-style widgets)**
  - `views/analysis_view.py`
  - `views/comparison_view.py`
  - `views/compression_view.py`
  - `views/network_view.py`
  - `views/dashboard_view.py`

- **Reusable controls**
  - `panels/resource_tree.py`
  - `panels/property_panel.py`
  - `panels/info_panel.py`
  - `views/visualizers/block.py`（块分析图）
  - `views/visualizers/huffman.py`（Huffman 树 / 画布等）
  - `helpers.py`（原 `label_utils` / `encoding_preview` 等合并至此）

## Naming rules (enforced incrementally)

- Window/dialog aggregators: `*_window.py` or `*_windows.py`
- Reusable widgets/panels: `*_view.py`, `*_panel.py`, `*_utils.py`
- Prefer `dialogs/<topic>_dialog.py` over the `visualization_windows` compatibility barrel.

## Deprecation note

- `legacy/main_window_legacy_backup.py` is archived reference code and must not be imported by runtime paths.


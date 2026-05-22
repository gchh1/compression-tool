# `gui.ui` ownership map

This directory (`src/gui/ui/`) is the desktop GUI presentation layer.

## Ownership by responsibility

- **Window composition / app shell**
  - `main_window.py` (main application window, workflow orchestration)

- **Modal dialogs**
  - `dialogs/viz_dialog.py` — LZ77 sliding-window visualization (`.viz` v2 files)
  - `dialogs/block_heatmap_dialog.py` — per-block token heatmap
  - `dialogs/heatmap_dialog.py` — full-file token heatmap
  - `dialogs/comparison_dialog.py` — algorithm comparison
  - `dialogs/network_sim_dialog.py` — network simulation
  - `dialogs/huffman_dialog.py` — Huffman tree viewer

- **View components**
  - `views/visualizers/token_heatmap.py` — token heatmap widget
  - `views/visualizers/huffman.py` — Huffman tree/canvas widget

- **Reusable controls**
  - `widgets/window_canvas.py` — sliding-window canvas widget
  - `panels/resource_tree.py` — file resource tree

## Naming rules

- Dialogs: `dialogs/<topic>_dialog.py`
- Reusable widgets: `widgets/<name>.py`
- Panels: `panels/<name>.py`

## Deprecation note

- `legacy/main_window_legacy_backup.py` is archived reference code and must not be imported by runtime paths.

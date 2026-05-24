"""Modal dialogs for the compression GUI.

Submodules are **not** imported eagerly from this package. Import explicitly, for example:

- ``from gui.ui.dialogs.heatmap_dialog import HeatmapDialog``
- ``from gui.ui.dialogs.block_heatmap_dialog import BlockHeatmapDialog``
- ``from gui.ui.dialogs.network_sim_dialog import NetworkSimDialog``
- ``from gui.ui.dialogs.comparison_dialog import ComparisonDialog``

DEFLATE visualization (sliding window + Huffman trees) now lives in
``gui.ui.views.viz_dashboard`` (QWebEngineView + D3.js dashboard).

Token heatmap widgets live in ``gui.ui.views.visualizers.token_heatmap``.
"""

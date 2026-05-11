"""Modal dialogs for the compression GUI.

Submodules are **not** imported eagerly from this package. Import explicitly, for example:

- ``from gui.ui.dialogs.heatmap_dialog import HeatmapDialog``
- ``from gui.ui.dialogs.huffman_dialog import HuffmanTreeDialog``
- ``from gui.ui.dialogs.lz_demo_dialog import LZDPDPDialog, LZSliderDialog``
- ``from gui.ui.dialogs.block_heatmap_dialog import BlockHeatmapDialog``
- ``from gui.ui.dialogs.network_sim_dialog import NetworkSimDialog``
- ``from gui.ui.dialogs.flate_demo_dialog import FlateDemoDialog``
- ``from gui.ui.dialogs.comparison_dialog import ComparisonDialog``

Token heatmap widgets live in ``gui.ui.views.visualizers.token_heatmap``; Huffman
visualizers and canvas/frequency chart in ``gui.ui.views.visualizers.huffman``.

``gui.ui.visualization_windows`` re-exports the compression-dialog public surface for
backward compatibility (see ``docs/gui_architecture.md`` §3.4.2); prefer explicit
``dialogs.*`` imports in new code.
"""

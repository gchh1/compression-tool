"""Compression visualization dialogs (re-exports for stable imports).

Prefer importing from ``gui.ui.dialogs.<module>`` directly.
"""

from gui.ui.dialogs.block_heatmap_dialog import BlockHeatmapCanvas, BlockHeatmapDialog
from gui.ui.dialogs.comparison_dialog import ComparisonDialog
from gui.ui.dialogs.flate_demo_dialog import FlateDemoDialog
from gui.ui.dialogs.heatmap_dialog import HeatmapDialog
from gui.ui.dialogs.huffman_dialog import HuffmanTreeDialog
from gui.ui.dialogs.lz_demo_dialog import (
    DPArrayBar,
    LZDPDPDialog,
    LZDPDPSliderWidget,
    LZSliderDialog,
    LZSliderWidget,
)
from gui.ui.dialogs.network_sim_dialog import NetworkSimDialog

__all__ = [
    "BlockHeatmapCanvas",
    "BlockHeatmapDialog",
    "ComparisonDialog",
    "DPArrayBar",
    "FlateDemoDialog",
    "HeatmapDialog",
    "HuffmanTreeDialog",
    "LZDPDPDialog",
    "LZDPDPSliderWidget",
    "LZSliderDialog",
    "LZSliderWidget",
    "NetworkSimDialog",
]

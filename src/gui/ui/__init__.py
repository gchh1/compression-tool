"""
PyQt6 UI layer.

- ``main_window`` — application shell
- ``worker`` / ``table`` — compression worker and file table
- ``views/`` — tab-level views and ``views/visualizers/`` for analysis widgets
- ``panels/`` — reusable panels (resource tree, info)
- ``helpers.py`` — small UI utilities (styled labels, encoding preview text)
- ``dialogs/`` — modal dialogs (LZ/Flate demos, block heatmap, comparison, network sim)
"""

__all__ = [
    "main_window",
    "worker",
    "table",
    "dialogs",
    "views",
    "panels",
    "helpers",
]

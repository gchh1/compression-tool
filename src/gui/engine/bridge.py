"""Load and cache the C++ ``core_engine`` extension module."""

from __future__ import annotations

import logging
import sys
from pathlib import Path

logger = logging.getLogger(__name__)

_core_engine = None


def get_core_engine():
    """Return the loaded ``core_engine`` module, or ``None`` if unavailable."""
    global _core_engine
    if _core_engine is not None:
        return _core_engine

    # engine/bridge.py -> engine -> gui -> src -> repo root
    _base = Path(__file__).resolve().parent.parent.parent.parent
    _candidates = [
        _base / "build" / "src" / "bindings" / "pybind",
        _base / "build_pybind" / "src" / "bindings" / "pybind",
        _base / "build" / "src" / "bindings",
        _base / "build_pybind" / "src" / "bindings",
        _base / "src",
    ]

    _meipass = getattr(sys, "_MEIPASS", None)
    if _meipass:
        _candidates.insert(0, Path(_meipass) / "core_engine")

    _ext_patterns = ("core_engine.cp312-win_amd64.pyd", "core_engine.*.so")
    for _p in _candidates:
        if not _p.is_dir():
            continue
        if any(tuple(_p.glob(pat)) for pat in _ext_patterns):
            sys.path.insert(0, str(_p))
            break

    try:
        import core_engine

        _core_engine = core_engine
        logger.info("C++ core_engine loaded from %s", core_engine.__file__)
        return _core_engine
    except ImportError:
        logger.warning("core_engine not available, using fallback")
        return None

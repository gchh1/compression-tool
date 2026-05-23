"""Load and cache the C++ ``core_engine_new`` (or legacy ``core_engine``) extension module."""

from __future__ import annotations

import importlib
import logging
import os
import sys
from pathlib import Path

logger = logging.getLogger(__name__)

_core_engine = None

# PyInstaller (run.bat): --add-data core_engine_new*.pyd;core_engine_new
_FROZEN_SUBDIR = {
    "core_engine_new": "core_engine_new",
    "core_engine": "core_engine_new",
}


def _repo_root() -> Path:
    return Path(__file__).resolve().parent.parent.parent.parent


def _build_candidates(base: Path) -> list[Path]:
    return [
        # pybind_new 目录优先（core_engine_new）
        base / "build_py" / "src" / "bindings" / "pybind_new",
        base / "build_debug" / "src" / "bindings" / "pybind_new",
        base / "build" / "src" / "bindings" / "pybind_new",
        base / "build_pybind" / "src" / "bindings" / "pybind_new",
        # 兼容旧版 pybind 目录
        base / "build_py" / "src" / "bindings" / "pybind",
        base / "build_debug" / "src" / "bindings" / "pybind",
        base / "build" / "src" / "bindings" / "pybind",
        base / "build_pybind" / "src" / "bindings" / "pybind",
        base / "build" / "src" / "bindings",
        base / "build_pybind" / "src" / "bindings",
        base / "Package" / "bin" / "core_engine",
        base / "Package" / "bin" / "core_engine_new",
        base / "src",
    ]


def _dir_has_module_pyd(directory: Path, module_name: str) -> bool:
    if not directory.is_dir():
        return False
    return any(directory.glob(f"{module_name}*.pyd"))


def _frozen_candidates(module_name: str) -> list[Path]:
    meipass = getattr(sys, "_MEIPASS", None)
    if not meipass:
        return []
    root = Path(meipass)
    sub = _FROZEN_SUBDIR.get(module_name, module_name)
    return [
        root / sub,
        root,
    ]


def _try_load_engine(candidates: list[Path], module_name: str) -> bool:
    """Try to load a pyd module from candidate directories."""
    global _core_engine

    seen: set[str] = set()
    ordered: list[Path] = []

    for p in candidates + _frozen_candidates(module_name):
        key = str(p.resolve()) if p.exists() else str(p)
        if key in seen:
            continue
        seen.add(key)
        ordered.append(p)

    for directory in ordered:
        if not _dir_has_module_pyd(directory, module_name):
            continue
        dir_s = str(directory)
        if dir_s not in sys.path:
            sys.path.insert(0, dir_s)
        break

    try:
        module = importlib.import_module(module_name)
        _core_engine = module
        logger.info(
            "C++ %s loaded from %s",
            module_name,
            getattr(module, "__file__", "unknown"),
        )
        return True
    except ImportError as e:
        logger.debug("import %s failed: %s", module_name, e)
        return False


def get_core_engine():
    """Return the loaded extension module, or ``None`` if unavailable.

    Priority:
      1. ``core_engine_new`` — links ``api_new`` + ``core_new`` + ``algorithm_new``
         for LZDP / LZSS / Deflate / DPFlate / WCX (new implementation).
      2. ``core_engine`` — legacy module (includes ADE support).
    """
    global _core_engine
    if _core_engine is not None:
        return _core_engine

    base = _repo_root()
    candidates = _build_candidates(base)

    # 优先加载新版引擎 core_engine_new
    if _try_load_engine(candidates, "core_engine_new"):
        logger.info("Using core_engine_new (new implementation)")
        return _core_engine

    # 回退到旧版 core_engine（兼容 ADE）
    if _try_load_engine(candidates, "core_engine"):
        logger.warning("core_engine_new not found, falling back to legacy core_engine")
        return _core_engine

    logger.warning("C++ core_engine not available")
    return None

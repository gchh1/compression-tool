"""Application resource paths (icons, etc.)."""

from __future__ import annotations

import sys
from pathlib import Path


def resolve_icon_path() -> Path | None:
    candidates: list[Path] = []
    if getattr(sys, "frozen", False):
        base = Path(sys.executable).parent
        candidates = [
            base / "WebCompressor.ico",
            base.parent / "resources" / "icon" / "WebCompressor.ico",
        ]
    else:
        base = Path(__file__).resolve().parent.parent.parent.parent
        candidates = [
            base / "resources" / "icon" / "WebCompressor.ico",
        ]
    for c in candidates:
        if c.exists():
            return c
    return None

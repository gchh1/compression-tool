import sys
from pathlib import Path


def resolve_icon_path() -> Path | None:
    if getattr(sys, "frozen", False):
        base = Path(sys.executable).parent.parent
    else:
        repo = Path(__file__).resolve().parent.parent.parent.parent
        base = repo / "resources" / "icon"

    candidates = [
        base / "WebCompressor.ico",
        base / "WebCompressor2.ico",
        base / "WebCompressor3.ico",
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate

    return None
"""Runtime workspace layout for streaming artifacts (see streaming-workspace-spec §5)."""

from __future__ import annotations

import re
import uuid
from pathlib import Path


def _config_dir() -> Path:
    """Directory that contains ``webcompress_settings.json`` (``…/config``)."""
    from gui.config.settings import _get_config_path

    return _get_config_path().parent


def workspace_root() -> Path:
    """Default root: sibling of the ``config`` directory, named ``workspace``."""
    return _config_dir().parent / "workspace"


def compressed_dir() -> Path:
    p = workspace_root() / "compressed"
    p.mkdir(parents=True, exist_ok=True)
    return p


def tmp_dir() -> Path:
    p = workspace_root() / "tmp"
    p.mkdir(parents=True, exist_ok=True)
    return p


def ensure_workspace_layout() -> None:
    """Create ``compressed/``, ``tmp/``, ``jobs/`` under the workspace root."""
    compressed_dir()
    tmp_dir()
    (workspace_root() / "jobs").mkdir(parents=True, exist_ok=True)


def _safe_stem(source_path: str, max_len: int = 96) -> str:
    stem = Path(source_path).stem
    stem = re.sub(r"[^\w.\-]", "_", stem, flags=re.UNICODE)
    if not stem:
        stem = "file"
    return stem[:max_len]


def allocate_streaming_wcx_path(source_path: str) -> Path:
    """Return a unique ``.wcx`` path under ``workspace/compressed/`` for C++ streaming output."""
    uid = uuid.uuid4().hex[:12]
    name = f"{uid}_{_safe_stem(source_path)}.wcx"
    return compressed_dir() / name

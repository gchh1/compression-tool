"""Runtime workspace layout for streaming artifacts (see streaming-workspace-spec §5, §11)."""

from __future__ import annotations

import logging
import os
import re
import shutil
import uuid
from pathlib import Path

logger = logging.getLogger(__name__)


def _config_dir() -> Path:
    """Directory that contains ``webcompress_settings.json`` (``…/config``)."""
    from gui.config.settings import _get_config_path

    return _get_config_path().parent


def workspace_root() -> Path:
    """Workspace root: ``streaming.workspace_root`` if set, else sibling ``config/../workspace``."""
    from gui.config.settings import get_streaming_workspace_root_override, load_config

    override = get_streaming_workspace_root_override(load_config())
    if override:
        return Path(override).expanduser().resolve()
    return _config_dir().parent / "workspace"


def compressed_dir() -> Path:
    p = workspace_root() / "compressed"
    p.mkdir(parents=True, exist_ok=True)
    return p


def tmp_dir() -> Path:
    p = workspace_root() / "tmp"
    p.mkdir(parents=True, exist_ok=True)
    return p


_layout_ensured = False


def ensure_workspace_layout() -> None:
    """Create ``compressed/``, ``tmp/``, ``jobs/``, ``decompressed/``; sweep stale artifacts on first call only."""
    global _layout_ensured
    compressed_dir()
    tmp_dir()
    (workspace_root() / "jobs").mkdir(parents=True, exist_ok=True)
    (workspace_root() / "decompressed").mkdir(parents=True, exist_ok=True)
    if not _layout_ensured:
        _layout_ensured = True
        sweep_workspace_transient_artifacts()
    # C++ DP spill (TempFile) reads this and writes under ``<root>/tmp/`` (see TempFile.hpp).
    os.environ["WEBCOMPRESS_WORKSPACE"] = os.fsdecode(workspace_root())


def remove_stale_compressed_parts() -> None:
    """Delete ``compressed/*.part`` (crash/interrupt leftovers)."""
    comp = workspace_root() / "compressed"
    if not comp.is_dir():
        return
    for p in comp.glob("*.part"):
        try:
            p.unlink(missing_ok=True)
        except OSError as e:
            logger.debug("[workspace] skip removing %s: %s", p, e)


def empty_tmp_dir() -> None:
    """Remove all entries under ``tmp/`` (best-effort)."""
    td = workspace_root() / "tmp"
    if not td.is_dir():
        return
    for child in td.iterdir():
        try:
            if child.is_symlink() or child.is_file():
                child.unlink(missing_ok=True)
            elif child.is_dir():
                shutil.rmtree(child, ignore_errors=True)
        except OSError as e:
            logger.debug("[workspace] skip removing %s: %s", child, e)


def sweep_workspace_transient_artifacts() -> None:
    """Startup sweep: stale ``.part`` in ``compressed/`` and scratch under ``tmp/``."""
    remove_stale_compressed_parts()
    empty_tmp_dir()


def cleanup_workspace_on_app_quit() -> None:
    """Graceful exit: clear ``tmp/`` (completed ``.wcx`` stay under ``compressed/``)."""
    empty_tmp_dir()


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


def viz_dir() -> Path:
    p = workspace_root() / "viz"
    p.mkdir(parents=True, exist_ok=True)
    return p


def allocate_viz_path(source_path: str) -> Path:
    """Return a unique ``.viz`` path under ``workspace/viz/``."""
    uid = uuid.uuid4().hex[:12]
    name = f"{uid}_{_safe_stem(source_path)}.viz"
    return viz_dir() / name


def sweep_viz_artifacts() -> None:
    """Delete all ``.viz`` files under ``workspace/viz/``."""
    vd = viz_dir()
    for p in vd.glob("*.viz"):
        try:
            p.unlink(missing_ok=True)
        except OSError as e:
            logger.debug("[workspace] skip removing %s: %s", p, e)

"""SIM L1/L2 checkpoint manifest for silent exploration (see streaming-interrupt-checkpoint-design.md)."""

from __future__ import annotations

import json
import logging
import time
import uuid
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any

logger = logging.getLogger(__name__)

MANIFEST_VERSION = 1


@dataclass
class ExploreDispatch:
    cluster_id: int = -1
    explore_type: str = ""
    parent_decision: str = ""
    target_algo: str = ""
    ucb_gap: float = 0.0


@dataclass
class CheckpointManifest:
    """Persisted when a low-priority streaming explore job is cooperatively cancelled."""

    job_id: str = ""
    job_kind: str = "explore"
    job_state: str = "cancelled"
    cancel_reason: str = "user_irq"
    bytes_total: int = 0
    bytes_read: int = 0
    bytes_written_part: int = 0
    staged_part_path: str = ""
    part_deleted: bool = True
    input_path: str = ""
    algorithm: str = ""
    algorithm_params: dict[str, int] = field(default_factory=dict)
    config_snapshot_json: str = ""
    dispatch: ExploreDispatch = field(default_factory=ExploreDispatch)
    elapsed_ms: float = 0.0
    pipeline_profile_hint: str = ""
    manifest_version: int = MANIFEST_VERSION
    created_at: float = field(default_factory=time.time)

    def to_dict(self) -> dict[str, Any]:
        d = asdict(self)
        return d

    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> CheckpointManifest:
        dispatch_raw = data.get("dispatch") or {}
        dispatch = ExploreDispatch(
            cluster_id=int(dispatch_raw.get("cluster_id", -1)),
            explore_type=str(dispatch_raw.get("explore_type", "")),
            parent_decision=str(dispatch_raw.get("parent_decision", "")),
            target_algo=str(dispatch_raw.get("target_algo", "")),
            ucb_gap=float(dispatch_raw.get("ucb_gap", 0.0)),
        )
        params = data.get("algorithm_params") or {}
        algo_params = {str(k): int(v) for k, v in params.items()}
        return cls(
            job_id=str(data.get("job_id", "")),
            job_kind=str(data.get("job_kind", "explore")),
            job_state=str(data.get("job_state", "cancelled")),
            cancel_reason=str(data.get("cancel_reason", "")),
            bytes_total=int(data.get("bytes_total", 0)),
            bytes_read=int(data.get("bytes_read", 0)),
            bytes_written_part=int(data.get("bytes_written_part", 0)),
            staged_part_path=str(data.get("staged_part_path", "")),
            part_deleted=bool(data.get("part_deleted", True)),
            input_path=str(data.get("input_path", "")),
            algorithm=str(data.get("algorithm", "")),
            algorithm_params=algo_params,
            config_snapshot_json=str(data.get("config_snapshot_json", "")),
            dispatch=dispatch,
            elapsed_ms=float(data.get("elapsed_ms", 0.0)),
            pipeline_profile_hint=str(data.get("pipeline_profile_hint", "")),
            manifest_version=int(data.get("manifest_version", MANIFEST_VERSION)),
            created_at=float(data.get("created_at", time.time())),
        )


def new_job_id(prefix: str = "explore") -> str:
    return f"{prefix}-{uuid.uuid4().hex[:12]}"


def jobs_dir() -> Path:
    from gui.utils.workspace import workspace_root

    p = workspace_root() / "jobs"
    p.mkdir(parents=True, exist_ok=True)
    return p


def manifest_path(job_id: str) -> Path:
    return jobs_dir() / f"{job_id}.checkpoint.json"


def save_manifest(manifest: CheckpointManifest) -> Path:
    if not manifest.job_id:
        manifest.job_id = new_job_id()
    path = manifest_path(manifest.job_id)
    with open(path, "w", encoding="utf-8") as f:
        json.dump(manifest.to_dict(), f, indent=2, ensure_ascii=False)
    logger.info("[checkpoint] saved %s", path)
    return path


def load_manifest(job_id: str) -> CheckpointManifest | None:
    path = manifest_path(job_id)
    if not path.is_file():
        return None
    with open(path, "r", encoding="utf-8") as f:
        return CheckpointManifest.from_dict(json.load(f))


def block_profile_hint(block_profile: object | None) -> str:
    if block_profile is None:
        return ""
    blocks = getattr(block_profile, "blocks", None)
    if blocks is None:
        return ""
    try:
        n = len(blocks)
        if n == 0:
            return "blocks=0"
        b0 = blocks[0]
        return (
            f"blocks={n} b0_literals={getattr(b0, 'literal_count', 0)} "
            f"b0_matches={getattr(b0, 'match_count', 0)}"
        )
    except Exception:
        return "blocks=?"

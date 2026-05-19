"""Streaming silent exploration + interrupt simulation (SIM v1)."""

from __future__ import annotations

import json
import logging
import os
import threading
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

from gui.ade.explore_log import log_explore, summarize_pipeline_result, summarize_stream_result
from gui.models import AlgorithmType, FileRecord

logger = logging.getLogger(__name__)


@dataclass
class ExploreStreamResult:
    success: bool
    cancelled: bool
    job_id: str
    wcx_path: str | None
    payload_bytes: int
    compression_ratio: float
    time_ms: float
    bytes_processed: int
    error_message: str
    manifest_path: str | None = None


def _set_stream_cancel(requested: bool) -> None:
    try:
        from gui.ui.worker import _core_set_streaming_compress_cancel

        _core_set_streaming_compress_cancel(bool(requested))
    except Exception:
        pass


def clear_simulate_cancel_env() -> str | None:
    """Remove test-only cancel env so normal compress runs to completion. Returns previous value."""
    return os.environ.pop("WEBCOMPRESS_SIMULATE_CANCEL_AFTER_BYTES", None)


def _resolve_input_path(record: FileRecord) -> str:
    if getattr(record, "path", None) and Path(record.path).is_file():
        return str(Path(record.path).resolve())
    from gui.utils.workspace import tmp_dir

    td = tmp_dir()
    safe = getattr(record, "name", "input.bin") or "input.bin"
    p = td / f"explore-input-{safe}"
    if not getattr(record, "raw_data", None):
        record.load_raw_data()
    p.write_bytes(record.raw_data or b"")
    return str(p.resolve())


def should_explore_use_streaming(
    record: FileRecord,
    *,
    force_stream: bool = False,
) -> bool:
    """Use file-to-file chunked I/O when explore file exceeds ADE threshold."""
    if force_stream:
        return True
    from gui.config.settings import get_ade_explore_streaming_threshold_mb, load_config

    size = int(getattr(record, "size", 0) or 0)
    if size <= 0 and getattr(record, "raw_data", None):
        size = len(record.raw_data)
    mb = get_ade_explore_streaming_threshold_mb(load_config())
    return size > int(mb * 1024 * 1024)


def explore_chunk_bytes() -> int:
    from gui.config.settings import get_ade_explore_streaming_chunk_kb, load_config

    return int(get_ade_explore_streaming_chunk_kb(load_config())) * 1024


def compress_streaming_to_wcx(
    record: FileRecord,
    algorithm: AlgorithmType,
    *,
    out_path: Path | None = None,
    honor_simulate_cancel_env: bool = False,
    chunk_bytes: int | None = None,
) -> tuple[Path, object]:
    """File-to-file streaming compress; returns (final_wcx_path, PipelineCompressResult)."""
    from gui.engine.compressor import CompressionEngine
    from gui.utils.workspace import tmp_dir

    engine = CompressionEngine()
    if not engine.available:
        raise RuntimeError("core_engine not available")

    input_path = _resolve_input_path(record)
    if out_path is None:
        out_path = tmp_dir() / f"explore-{algorithm.value}.wcx"
    out_path = Path(out_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    if not honor_simulate_cancel_env:
        clear_simulate_cancel_env()
    _set_stream_cancel(False)
    eff_chunk = int(chunk_bytes) if chunk_bytes and chunk_bytes > 0 else explore_chunk_bytes()
    log_explore(
        "stream_compress_begin",
        algorithm=algorithm.value,
        input_path=input_path,
        out_path=str(out_path),
        chunk_bytes=eff_chunk,
        honor_sim_cancel_env=honor_simulate_cancel_env,
    )
    result = engine.smart_compress_file(
        input_path,
        str(out_path),
        algorithm,
        chunk_bytes_override=eff_chunk,
    )
    log_explore(
        "stream_compress_end",
        algorithm=algorithm.value,
        out_path=str(out_path),
        **summarize_pipeline_result(result),
    )
    return out_path, result


def compress_memory_to_wcx_bytes(
    record: FileRecord,
    algorithm: AlgorithmType,
) -> tuple[bytes, object]:
    """In-memory pipeline/one-shot compress, wrap with C++ pack_wcx at completion."""
    from gui.engine.compressor import CompressionEngine
    from gui.engine.file_protocol import finalize_codec_payload_to_wcx

    if not getattr(record, "raw_data", None):
        record.load_raw_data()
    from gui.config.settings import get_use_web_resource_dict
    from gui.engine.web_dict import prepare_file_record_for_compression

    web_dict = False
    if get_use_web_resource_dict() and algorithm != AlgorithmType.NONE:
        web_dict = bool(prepare_file_record_for_compression(record))
    clear_simulate_cancel_env()
    _set_stream_cancel(False)
    engine = CompressionEngine()
    if not engine.available:
        raise RuntimeError("core_engine not available")
    result = engine.smart_compress(record.raw_data, algorithm)
    payload = bytes(result.data) if not isinstance(result.data, bytes) else result.data
    orig = int(getattr(record, "size", 0) or len(record.raw_data))
    wcx = finalize_codec_payload_to_wcx(
        payload,
        algorithm,
        orig,
        getattr(record, "name", "") or "",
        web_dict_preprocess=web_dict,
    )
    return wcx, result


def run_explore_stream_with_optional_cancel(
    record: FileRecord,
    algorithm: AlgorithmType,
    *,
    job_id: str,
    cancel_after_bytes: int = 0,
    cancel_reason: str = "simulated_irq",
    on_manifest: Callable[..., None] | None = None,
    dispatch_meta: dict | None = None,
    explore_config_snapshot: dict | None = None,
) -> ExploreStreamResult:
    """
    Streaming explore job. When ``cancel_after_bytes > 0``, sets
    ``WEBCOMPRESS_SIMULATE_CANCEL_AFTER_BYTES`` for deterministic C++ cancel.
    """
    from gui.ade.checkpoint import (
        CheckpointManifest,
        ExploreDispatch,
        block_profile_hint,
        save_manifest,
    )
    from gui.utils.workspace import tmp_dir

    part_final = tmp_dir() / f"{job_id}.wcx"
    part_staged = Path(str(part_final) + ".part")
    file_name = getattr(record, "name", "") or ""

    use_stream = should_explore_use_streaming(
        record, force_stream=(cancel_after_bytes > 0)
    )
    eff_chunk = explore_chunk_bytes()

    log_explore(
        "stream_job_start",
        job_id=job_id,
        file=file_name,
        algorithm=algorithm.value,
        cancel_after_bytes=cancel_after_bytes,
        io_mode="stream" if use_stream else "memory",
        explore_chunk_bytes=eff_chunk if use_stream else 0,
        file_size=getattr(record, "size", 0),
        out_path=str(part_final),
    )

    prev_env = os.environ.get("WEBCOMPRESS_SIMULATE_CANCEL_AFTER_BYTES")
    if cancel_after_bytes > 0:
        os.environ["WEBCOMPRESS_SIMULATE_CANCEL_AFTER_BYTES"] = str(int(cancel_after_bytes))
    else:
        os.environ.pop("WEBCOMPRESS_SIMULATE_CANCEL_AFTER_BYTES", None)

    _set_stream_cancel(False)
    t0 = time.perf_counter()
    out_path = part_final
    try:
        if use_stream:
            out_path, result = compress_streaming_to_wcx(
                record,
                algorithm,
                out_path=part_final,
                honor_simulate_cancel_env=(cancel_after_bytes > 0),
                chunk_bytes=eff_chunk,
            )
        else:
            wcx_bytes, result = compress_memory_to_wcx_bytes(record, algorithm)
            part_final.write_bytes(wcx_bytes)
            out_path = part_final
    finally:
        if prev_env is None:
            os.environ.pop("WEBCOMPRESS_SIMULATE_CANCEL_AFTER_BYTES", None)
        else:
            os.environ["WEBCOMPRESS_SIMULATE_CANCEL_AFTER_BYTES"] = prev_env
        _set_stream_cancel(False)

    elapsed_ms = (time.perf_counter() - t0) * 1000.0
    cancelled = bool(getattr(result, "cancelled", False)) or (
        not getattr(result, "success", True)
        and "cancel" in (getattr(result, "error_message", "") or "").lower()
    )
    bytes_proc = int(getattr(result, "bytes_processed", 0) or 0)
    comp_sz = int(getattr(result, "compressed_size", 0) or 0)
    input_path = _resolve_input_path(record)

    manifest_path_str: str | None = None
    if cancelled:
        disp = dispatch_meta or {}
        manifest = CheckpointManifest(
            job_id=job_id,
            job_state="cancelled",
            cancel_reason=cancel_reason,
            bytes_total=int(getattr(record, "size", 0) or 0),
            bytes_read=bytes_proc,
            bytes_written_part=comp_sz,
            staged_part_path=str(part_staged),
            part_deleted=not part_staged.is_file(),
            input_path=input_path,
            algorithm=algorithm.value,
            algorithm_params=dict(
                (explore_config_snapshot or {}).get(algorithm.value, {})
                if explore_config_snapshot
                else {}
            ),
            config_snapshot_json=json.dumps(explore_config_snapshot or {}, ensure_ascii=False),
            dispatch=ExploreDispatch(
                cluster_id=int(disp.get("cluster_id", -1)),
                explore_type=str(disp.get("explore_type", "")),
                parent_decision=str(disp.get("parent_decision", "")),
                target_algo=algorithm.value,
                ucb_gap=float(disp.get("ucb_gap", 0.0)),
            ),
            elapsed_ms=elapsed_ms,
            pipeline_profile_hint=block_profile_hint(getattr(result, "block_profile", None)),
        )
        manifest_path_str = str(save_manifest(manifest))
        log_explore(
            "stream_checkpoint",
            job_id=job_id,
            file=file_name,
            manifest_path=manifest_path_str,
            bytes_read=bytes_proc,
            bytes_written_part=comp_sz,
            cancel_reason=cancel_reason,
        )
        if on_manifest:
            on_manifest(manifest)

    wcx_ok = out_path.is_file() and getattr(result, "success", False)
    stream_out = ExploreStreamResult(
        success=bool(wcx_ok),
        cancelled=cancelled,
        job_id=job_id,
        wcx_path=str(out_path) if wcx_ok else None,
        payload_bytes=comp_sz,
        compression_ratio=float(getattr(result, "compression_ratio", 1.0) or 1.0),
        time_ms=float(getattr(result, "time_ms", 0.0) or 0.0),
        bytes_processed=bytes_proc,
        error_message=(getattr(result, "error_message", None) or "").strip(),
        manifest_path=manifest_path_str,
    )
    log_explore(
        "stream_job_end",
        file=file_name,
        **summarize_stream_result(stream_out),
    )
    return stream_out


def decompress_wcx_to_bytes(wcx_bytes: bytes, algorithm: AlgorithmType) -> bytes:
    from gui.engine.compressor import CompressionEngine
    from gui.engine.file_protocol import unpack_compressed_file

    _hdr, payload = unpack_compressed_file(wcx_bytes)
    engine = CompressionEngine()
    if not engine.available:
        raise RuntimeError("core_engine not available")
    dr = engine.smart_decompress(payload, algorithm)
    if not getattr(dr, "success", True):
        raise RuntimeError(dr.error_message or "decompress failed")
    data = dr.data
    return bytes(data) if not isinstance(data, bytes) else data


def wcx_codec_payload(wcx_bytes: bytes) -> bytes:
    from gui.engine.file_protocol import unpack_compressed_file

    _hdr, payload = unpack_compressed_file(wcx_bytes)
    return payload

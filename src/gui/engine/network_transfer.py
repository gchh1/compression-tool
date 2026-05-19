"""Real throttled network delivery + client decompress benchmark (F8)."""

from __future__ import annotations

import logging
import time
from dataclasses import dataclass
from typing import Callable

from gui.models import (
    AlgorithmType,
    CompressionStatus,
    FileRecord,
    FolderRecord,
    NetworkProfile,
    NETWORK_PROFILES,
    compressed_payload_size,
)
from gui.engine.file_protocol import MAGIC, file_record_compression_blob, unpack_compressed_file

logger = logging.getLogger(__name__)

ProgressCallback = Callable[[str, int, int], None]  # message, current, total
CancelCallback = Callable[[], bool]


@dataclass(frozen=True)
class NetworkBenchmarkItem:
    name: str
    raw_bytes: bytes
    wire_bytes: bytes
    algorithm: AlgorithmType
    is_stored: bool


@dataclass(frozen=True)
class NetworkBenchmarkTarget:
    label: str
    algorithm: str
    scope_note: str
    compression_time_ms: float
    items: tuple[NetworkBenchmarkItem, ...]


@dataclass
class ProfileBenchmarkResult:
    name: str
    bandwidth_mbps: float
    latency_ms: float
    raw_transfer_s: float
    comp_transfer_s: float
    decompress_s: float
    total_raw_path_s: float
    total_comp_path_s: float
    net_vs_raw_s: float
    worth_it: bool


def throttled_deliver(
    payload: bytes,
    profile: NetworkProfile,
    *,
    chunk_size: int = 16 * 1024,
    cancel: CancelCallback | None = None,
    on_chunk: Callable[[int, int], None] | None = None,
) -> tuple[bytes, float]:
    """Simulate sending ``payload`` over a link capped at ``profile`` bandwidth + latency."""
    if not payload:
        return b"", 0.0

    if profile.latency_ms > 0:
        time.sleep(profile.latency_ms / 1000.0)
        if cancel and cancel():
            raise InterruptedError("cancelled")

    bytes_per_sec = max(profile.bandwidth_bps / 8.0, 1.0)
    out = bytearray(len(payload))
    start = time.perf_counter()
    offset = 0
    total = len(payload)

    while offset < total:
        if cancel and cancel():
            raise InterruptedError("cancelled")
        end = min(offset + chunk_size, total)
        out[offset:end] = payload[offset:end]
        offset = end
        expected = len(out) / bytes_per_sec
        elapsed = time.perf_counter() - start
        if expected > elapsed:
            time.sleep(expected - elapsed)
        if on_chunk:
            on_chunk(offset, total)

    transfer_s = time.perf_counter() - start
    return bytes(out), transfer_s


def _decompress_wire_bytes(engine, wire: bytes, algorithm: AlgorithmType) -> float:
    if algorithm == AlgorithmType.NONE:
        return 0.0
    t0 = time.perf_counter()
    if len(wire) >= 4 and wire[:4] == MAGIC:
        header, payload = unpack_compressed_file(wire)
        algo = header.algorithm
    else:
        payload = wire
        algo = algorithm
    from gui.ade.explorer import SilentExplorer

    with SilentExplorer.user_compression_priority():
        result = engine.smart_decompress(payload, algo)
    if not getattr(result, "success", True):
        msg = (getattr(result, "error_message", None) or "").strip() or "decompress failed"
        raise RuntimeError(msg)
    _ = bytes(result.data)
    return time.perf_counter() - t0


def benchmark_profile(
    target: NetworkBenchmarkTarget,
    profile: NetworkProfile,
    engine,
    *,
    cancel: CancelCallback | None = None,
    progress: ProgressCallback | None = None,
) -> ProfileBenchmarkResult:
    raw_transfer_s = 0.0
    comp_transfer_s = 0.0
    decompress_s = 0.0
    n = len(target.items)

    for i, item in enumerate(target.items):
        if cancel and cancel():
            raise InterruptedError("cancelled")
        if progress:
            progress(f"{profile.name}: {item.name}", i, n)

        _, t_raw = throttled_deliver(
            item.raw_bytes,
            profile,
            cancel=cancel,
        )
        raw_transfer_s += t_raw

        if item.is_stored or not item.wire_bytes:
            _, t_comp = throttled_deliver(item.raw_bytes, profile, cancel=cancel)
            comp_transfer_s += t_comp
            continue

        received, t_comp = throttled_deliver(
            item.wire_bytes,
            profile,
            cancel=cancel,
        )
        comp_transfer_s += t_comp
        decompress_s += _decompress_wire_bytes(engine, received, item.algorithm)

    total_raw = raw_transfer_s
    total_comp = comp_transfer_s + decompress_s
    net = total_raw - total_comp
    return ProfileBenchmarkResult(
        name=profile.name,
        bandwidth_mbps=profile.bandwidth_bps / 1_000_000,
        latency_ms=profile.latency_ms,
        raw_transfer_s=raw_transfer_s,
        comp_transfer_s=comp_transfer_s,
        decompress_s=decompress_s,
        total_raw_path_s=total_raw,
        total_comp_path_s=total_comp,
        net_vs_raw_s=net,
        worth_it=net > 0,
    )


def benchmark_all_profiles(
    target: NetworkBenchmarkTarget,
    engine,
    *,
    cancel: CancelCallback | None = None,
    progress: ProgressCallback | None = None,
) -> list[ProfileBenchmarkResult]:
    results: list[ProfileBenchmarkResult] = []
    profiles = list(NETWORK_PROFILES.items())
    for pi, (_, profile) in enumerate(profiles):
        if cancel and cancel():
            raise InterruptedError("cancelled")
        if progress:
            progress(f"网络环境 {profile.name}", pi, len(profiles))

        def _prog(msg: str, cur: int, tot: int) -> None:
            if progress:
                progress(msg, cur, tot)

        results.append(
            benchmark_profile(
                target,
                profile,
                engine,
                cancel=cancel,
                progress=_prog,
            )
        )
    return results


def item_from_file_record(record: FileRecord) -> NetworkBenchmarkItem | None:
    if record.status != CompressionStatus.DONE:
        return None
    record.load_raw_data()
    raw = bytes(record.raw_data or b"")
    if not raw:
        return None
    wire = file_record_compression_blob(record)
    if wire is None and getattr(record, "compressed_path", None):
        try:
            from pathlib import Path

            wire = Path(record.compressed_path).read_bytes()
        except OSError:
            wire = None
    is_stored = bool(getattr(record, "is_stored", False))
    if is_stored:
        wire = raw
    elif not wire or compressed_payload_size(record) <= 0:
        return None
    else:
        wire = bytes(wire)
    return NetworkBenchmarkItem(
        name=record.name,
        raw_bytes=raw,
        wire_bytes=wire,
        algorithm=record.algorithm,
        is_stored=is_stored,
    )


def target_from_file_record(record: FileRecord) -> NetworkBenchmarkTarget | None:
    item = item_from_file_record(record)
    if item is None:
        return None
    comp_sz = compressed_payload_size(record)
    return NetworkBenchmarkTarget(
        label=record.name,
        algorithm=record.algorithm.value,
        scope_note="",
        compression_time_ms=float(record.compression_time_ms or 0),
        items=(item,),
    )


def target_from_folder_record(record: FolderRecord) -> NetworkBenchmarkTarget | None:
    record.ensure_files_loaded()
    items: list[NetworkBenchmarkItem] = []
    algos: set[str] = set()
    time_ms = 0.0
    for f in record.files:
        it = item_from_file_record(f)
        if it is None:
            continue
        items.append(it)
        time_ms += float(f.compression_time_ms or 0)
        algos.add(f.algorithm.value)
    if not items:
        return None
    algo = ", ".join(sorted(algos))
    note = f"网页整站顺序加载 · {len(items)} 个资源（限速传输 + 客户端解压）"
    return NetworkBenchmarkTarget(
        label=record.name,
        algorithm=algo,
        scope_note=note,
        compression_time_ms=time_ms,
        items=tuple(items),
    )

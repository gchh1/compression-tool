#!/usr/bin/env python3
"""
ADE SIM: interrupt checkpoint + streaming vs memory WCX consistency.

1. Uninterrupted file-to-file streaming vs memory-pack WCX: codec payload byte-identical;
   decompressed plaintext matches source (full file).
2. Simulated interrupt (WEBCOMPRESS_SIMULATE_CANCEL_AFTER_BYTES): checkpoint manifest
   written; result is not byte-equal to completed WCX.

Usage::
    python tests/python/test_ade_interrupt_checkpoint_parity.py [path_to_test_file]

Requires ``src`` on PYTHONPATH and built ``core_engine``.

Do **not** leave ``WEBCOMPRESS_SIMULATE_CANCEL_AFTER_BYTES`` set in the shell when
running this script — section B sets cancel internally. If you set it globally, section A
will fail with ``cancelled``.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path

_HERE = Path(__file__).resolve().parent
_REPO = _HERE.parents[1]
_SRC = _REPO / "src"
if str(_SRC) not in sys.path:
    sys.path.insert(0, str(_SRC))


def _pick_corpus(extra: Path | None) -> Path:
    if extra and extra.is_file():
        return extra
    for cand in (
        _REPO / "resources" / "imdb-movie-reviews-word2vec-tfidf-bow.ipynb",
        _REPO / "tests" / "data" / "repeat_1mb.bin",
    ):
        if cand.is_file():
            return cand
    # synthetic
    p = _REPO / "tests" / "data" / "_ade_parity_synthetic.bin"
    p.parent.mkdir(parents=True, exist_ok=True)
    if not p.is_file():
        p.write_bytes(b"ADE_PARITY_" * 50000)
    return p


def main() -> int:
    from gui.models import AlgorithmType, FileRecord
    from gui.engine.compressor import CompressionEngine
    from gui.ade.checkpoint import load_manifest
    from gui.ade.checkpoint import new_job_id
    from gui.ade.streaming_explore import (
        compress_memory_to_wcx_bytes,
        compress_streaming_to_wcx,
        decompress_wcx_to_bytes,
        run_explore_stream_with_optional_cancel,
        wcx_codec_payload,
    )
    from gui.utils.workspace import ensure_workspace_layout

    extra = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else None
    corpus = _pick_corpus(extra)
    raw = corpus.read_bytes()

    ensure_workspace_layout()
    CompressionEngine.reload_from_file()

    engine = CompressionEngine()
    if not engine.available:
        print("SKIP: core_engine not available")
        return 0

    algo = AlgorithmType.LZDP
    record = FileRecord(str(corpus))
    record.raw_data = raw
    record.size = len(raw)
    record.algorithm = algo

    print("=" * 72)
    print(f"  corpus: {corpus.name} ({len(raw)} bytes) algo={algo.value}")
    print("=" * 72)

    from gui.ade.streaming_explore import clear_simulate_cancel_env

    if os.environ.get("WEBCOMPRESS_SIMULATE_CANCEL_AFTER_BYTES"):
        print(
            "  note: clearing WEBCOMPRESS_SIMULATE_CANCEL_AFTER_BYTES for section A "
            "(uninterrupted); section B sets its own threshold"
        )
    clear_simulate_cancel_env()

    # --- A: uninterrupted streaming vs memory WCX ---
    stream_path, stream_result = compress_streaming_to_wcx(record, algo)
    if not getattr(stream_result, "success", True):
        print("FAIL: streaming compress:", stream_result.error_message)
        return 1
    stream_wcx = stream_path.read_bytes()

    mem_wcx, mem_result = compress_memory_to_wcx_bytes(record, algo)
    if not getattr(mem_result, "success", True):
        print("FAIL: memory compress:", mem_result.error_message)
        return 1

    stream_payload = wcx_codec_payload(stream_wcx)
    mem_payload = wcx_codec_payload(mem_wcx)
    print(f"  stream payload: {len(stream_payload)} bytes")
    print(f"  memory payload: {len(mem_payload)} bytes")

    if stream_payload != mem_payload:
        print("FAIL: stream vs memory codec payload mismatch")
        n = min(len(stream_payload), len(mem_payload))
        for i in range(n):
            if stream_payload[i] != mem_payload[i]:
                print(f"  first diff @ {i}: stream=0x{stream_payload[i]:02x} mem=0x{mem_payload[i]:02x}")
                break
        return 1
    print("PASS: stream payload == memory payload")

    dec_stream = decompress_wcx_to_bytes(stream_wcx, algo)
    dec_mem = decompress_wcx_to_bytes(mem_wcx, algo)
    if dec_stream != raw:
        print(f"FAIL: stream roundtrip size {len(dec_stream)} != {len(raw)}")
        return 1
    if dec_mem != raw:
        print(f"FAIL: memory roundtrip size {len(dec_mem)} != {len(raw)}")
        return 1
    if dec_stream != dec_mem:
        print("FAIL: decompressed plaintext stream != memory")
        return 1
    print("PASS: full-file plaintext match (stream & memory vs source)")

    # --- B: simulated interrupt ---
    cancel_at = max(4096, len(raw) // 3)
    job_id = new_job_id("sim")
    intr = run_explore_stream_with_optional_cancel(
        record,
        algo,
        job_id=job_id,
        cancel_after_bytes=cancel_at,
        cancel_reason="simulated_irq_test",
        dispatch_meta={"cluster_id": 0, "explore_type": "L2-test"},
        explore_config_snapshot={algo.value: CompressionEngine.snapshot_for_algorithm(algo)},
    )

    if not intr.cancelled:
        print(
            "FAIL: expected cooperative cancel "
            f"(cancel_after_bytes={cancel_at}) got success="
            f"{intr.success} err={intr.error_message!r}"
        )
        return 1
    print(f"PASS: interrupt simulated bytes_read={intr.bytes_processed} (target~{cancel_at})")

    manifest = load_manifest(job_id)
    if manifest is None:
        print("FAIL: checkpoint manifest missing")
        return 1
    if manifest.bytes_read <= 0:
        print("FAIL: manifest.bytes_read not set")
        return 1
    if not manifest.part_deleted and Path(manifest.staged_part_path).is_file():
        print("WARN: .part still present (debug); manifest marks part_deleted=", manifest.part_deleted)
    print(f"PASS: checkpoint saved {manifest.job_id} bytes_read={manifest.bytes_read}")

    if intr.wcx_path and Path(intr.wcx_path).is_file():
        intr_wcx = Path(intr.wcx_path).read_bytes()
        if wcx_codec_payload(intr_wcx) == stream_payload:
            print("FAIL: interrupted job produced full payload (should differ)")
            return 1
        print("PASS: interrupted WCX payload != completed stream payload")
    else:
        print("PASS: no committed WCX after cancel (expected)")

    print("=" * 72)
    print("  ALL CHECKS PASSED")
    print("=" * 72)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

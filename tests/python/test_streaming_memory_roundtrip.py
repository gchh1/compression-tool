#!/usr/bin/env python3
"""
Streaming vs one-shot (memory) compression experiment.

- Algorithms: DEFLATE, LZSS, LZDP, DPFLATE (native core_engine).
- For each: one-shot ``compress()`` vs memory ``pipeline_compress()`` vs disk ``smart_compress_file()``.
- Reports compression ratio (compressed/original); **payload bytes** one-shot vs framed usually differ.
- Roundtrip: ``smart_decompress`` / ``pipeline_decompress`` on each payload; output must equal original **byte-for-byte**.

Usage::
    python tests/python/test_streaming_memory_roundtrip.py [optional_path_to_file]

Requires ``src`` on ``PYTHONPATH`` and a loadable ``core_engine`` extension.
"""

from __future__ import annotations

import os
import random
import sys
import tempfile
from pathlib import Path

# repo roots: .../compression-tool/tests/python/this_file.py
_HERE = Path(__file__).resolve().parent
_REPO = _HERE.parents[1]
_SRC = _REPO / "src"
if str(_SRC) not in sys.path:
    sys.path.insert(0, str(_SRC))


def _datasets(extra_path: Path | None) -> dict[str, bytes]:
    random.seed(12345)
    d: dict[str, bytes] = {
        "tiny_text": (b"Hello stream vs memory " * 50),
        "med_text": (b"The quick brown fox jumps over the lazy dog.\n" * 2000),
        "med_random": bytes(random.randint(0, 255) for _ in range(50_000)),
    }
    if extra_path and extra_path.is_file():
        d["user_file"] = extra_path.read_bytes()
    return d


def _b(r) -> bytes:
    if r is None:
        return b""
    d = getattr(r, "data", None)
    if d is None:
        return b""
    return bytes(d)


def main() -> int:
    from gui.models import AlgorithmType
    from gui.engine.compressor import CompressionEngine
    from gui.engine.file_protocol import (
        unpack_compressed_file,
        is_u32_be_chunk_framed_stream_payload,
    )

    extra = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else None
    datasets = _datasets(extra)

    engine = CompressionEngine()
    if not engine.available:
        print("SKIP: core_engine not available (build / PYTHONPATH / extension).")
        return 0

    algos = [
        AlgorithmType.DEFLATE,
        AlgorithmType.LZSS,
        AlgorithmType.LZDP,
        AlgorithmType.DPFLATE,
    ]

    print("=" * 88)
    print("  streaming vs memory compression experiment")
    print("=" * 88)

    for name, raw in datasets.items():
        print(f"\n--- dataset {name!r}  original_bytes={len(raw):,} ---")
        for algo in algos:
            label = algo.value
            try:
                one = engine.compress(raw, algo)
                if not one.success:
                    print(f"  [{label}] one_shot compress FAIL: {one.error_message}")
                    continue
                p_one = _b(one)
                r_one = one.compressed_size / max(1, len(raw))

                pipe = engine.pipeline_compress(raw, algo)
                if not pipe.success:
                    print(f"  [{label}] pipeline_compress FAIL: {pipe.error_message}")
                    continue
                p_pipe = _b(pipe)
                r_pipe = pipe.compressed_size / max(1, len(raw))

                with tempfile.TemporaryDirectory() as td:
                    tin = os.path.join(td, "in.bin")
                    twcx = os.path.join(td, "out.wcx")
                    with open(tin, "wb") as f:
                        f.write(raw)
                    fcr = engine.smart_compress_file(tin, twcx, algo)
                    if not fcr.success:
                        print(f"  [{label}] smart_compress_file FAIL: {fcr.error_message}")
                        continue
                    wcx_bytes = Path(twcx).read_bytes()
                    hdr, p_file = unpack_compressed_file(wcx_bytes)
                    r_file = len(p_file) / max(1, len(raw))

                framed_pipe = is_u32_be_chunk_framed_stream_payload(p_pipe)
                framed_file = is_u32_be_chunk_framed_stream_payload(p_file)
                same_payload_po_pf = p_one == p_file
                same_payload_pp_pf = p_pipe == p_file

                d_mem = engine.smart_decompress(p_one, algo)
                d_pipe = engine.smart_decompress(p_pipe, algo)
                d_file = engine.smart_decompress(p_file, algo)

                ok_mem = d_mem.success and _b(d_mem) == raw
                ok_pipe = d_pipe.success and _b(d_pipe) == raw
                ok_file = d_file.success and _b(d_file) == raw

                print(
                    f"  [{label}] ratio_one={r_one:.6f} ratio_pipe={r_pipe:.6f} ratio_file={r_file:.6f} | "
                    f"framed(pipe,file)=({framed_pipe},{framed_file}) | "
                    f"bytes_equal(one,file)={same_payload_po_pf} (pipe,file)={same_payload_pp_pf}"
                )
                print(
                    f"         roundtrip one_shot={ok_mem} pipeline_mem={ok_pipe} file_stream={ok_file}"
                )
                if not (ok_mem and ok_pipe and ok_file):
                    print(f"         ERR one={getattr(d_mem,'error_message',None)} pipe={getattr(d_pipe,'error_message',None)} file={getattr(d_file,'error_message',None)}")
            except Exception as e:
                print(f"  [{label}] EXCEPTION: {e}")

    print("\nDone.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

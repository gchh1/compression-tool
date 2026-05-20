#!/usr/bin/env python3
"""Deflate + 3HfMT round-trip: memory / stream, WCX pack, INFLATE decompress (GUI path)."""

from __future__ import annotations

import argparse
import os
import struct
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PYBIND = ROOT / "build_py" / "src" / "bindings" / "pybind"
if PYBIND.is_dir():
    sys.path.insert(0, str(PYBIND))


def gui_deflate_params(engine, *, use_3hfmtree: bool) -> object:
    """Mirror webcompress_settings.json deflate section; toggle tree backend."""
    p = engine.DeflatePipelineParams()
    p.search_size = 32767
    p.lookahead_size = 255
    p.min_match = 0
    p.max_chain_length = 256
    p.use_flag_encoding = True
    p.use_3hfmtree = bool(use_3hfmtree)
    p.huffman_offset_chunk_bits = 8
    p.huffman_length_chunk_bits = 8
    return p


def deflate_compressor(engine, *, use_3hfmtree: bool):
    c = engine.DeflateCompressor()
    c.set_search_size(32767)
    c.set_lookahead_size(255)
    c.set_min_match(0)
    c.set_use_flag_encoding(True)
    c.set_use_3hfmtree(use_3hfmtree)
    c.set_huffman_offset_chunk_bits(8)
    c.set_huffman_length_chunk_bits(8)
    return c


def wcx_payload(wcx_bytes: bytes) -> bytes:
    if len(wcx_bytes) < 18 or wcx_bytes[:4] != b"WCMP":
        return wcx_bytes
    fnlen = struct.unpack_from("<4sBBIIBHB", wcx_bytes, 0)[6]
    return wcx_bytes[18 + fnlen :]


def check(name: str, ok: bool, detail: str = "") -> bool:
    mark = "PASS" if ok else "FAIL"
    line = f"[{mark}] {name}"
    if detail:
        line += f" — {detail}"
    print(line)
    return ok


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "input",
        nargs="?",
        default=str(ROOT / "resources" / "imdb-movie-reviews-word2vec-tfidf-bow.ipynb"),
    )
    parser.add_argument("--chunk-kb", type=int, default=512)
    args = parser.parse_args()

    try:
        import core_engine as e
    except ImportError as err:
        print(f"Cannot import core_engine: {err}")
        print(f"Build first: cmake --build {ROOT / 'build_py'} --target core_engine")
        return 2

    src = Path(args.input)
    if not src.is_file():
        print(f"Input not found: {src}")
        return 2

    orig = src.read_bytes()
    p3 = gui_deflate_params(e, use_3hfmtree=True)
    p0 = gui_deflate_params(e, use_3hfmtree=False)
    chunk = max(65536, int(args.chunk_kb) * 1024)
    all_ok = True

    print(f"corpus: {src.name} ({len(orig)} bytes) chunk={chunk} tree=3HfMT")

    # 0) 3HfMT vs FLATE payloads must differ (sanity)
    c3 = bytes(e.pipeline_compress(bytes(orig), [e.AlgorithmID.DEFLATE], None, None, p3, None, 0).data)
    c0 = bytes(e.pipeline_compress(bytes(orig), [e.AlgorithmID.DEFLATE], None, None, p0, None, 0).data)
    all_ok &= check(
        "3HfMT vs FLATE compressed size differs",
        len(c3) != len(c0) or c3 != c0,
        f"3hm={len(c3)} flate={len(c0)}",
    )

    # 1) Memory pipeline (DEFLATE compress, INFLATE decompress — GUI mapping)
    mem_payload = c3
    dr = e.pipeline_decompress(mem_payload, [e.AlgorithmID.INFLATE], None, None, p3, None, 0)
    all_ok &= check(
        "memory pipeline roundtrip (INFLATE + deflate params)",
        bytes(dr.data) == orig,
        f"got {len(dr.data)} expected {len(orig)}",
    )

    # Wrong tree on decompress must not restore
    dr_wrong = e.pipeline_decompress(mem_payload, [e.AlgorithmID.INFLATE], None, None, p0, None, 0)
    all_ok &= check(
        "FLATE params cannot decode 3HfMT payload",
        bytes(dr_wrong.data) != orig,
        "regression: use_3hfmtree must match",
    )

    td = tempfile.mkdtemp(prefix="deflate_3hm_wcx_")
    try:
        inp = os.path.join(td, "in.bin")
        wcx_path = os.path.join(td, "out.wcx")
        dec_path = os.path.join(td, "out.bin")
        Path(inp).write_bytes(orig)

        fr = e.pipeline_compress_file(
            inp, wcx_path, [e.AlgorithmID.DEFLATE], chunk, 0, None, None, p3, None
        )
        all_ok &= check("stream compressFile success", bool(getattr(fr, "success", False)))
        stream_payload = wcx_payload(Path(wcx_path).read_bytes())
        if stream_payload != mem_payload:
            print(
                f"[INFO] stream vs memory payload differ: "
                f"stream={len(stream_payload)} mem={len(mem_payload)}"
            )

        dr2 = e.pipeline_decompress(
            stream_payload, [e.AlgorithmID.INFLATE], None, None, p3, None, 0
        )
        all_ok &= check(
            "pipeline_decompress(WCX payload)",
            bytes(dr2.data) == orig,
            f"got {len(dr2.data)}",
        )

        packed = bytes(
            e.pack_wcx(stream_payload, e.AlgorithmID.DEFLATE, len(orig), src.name, False, False)
        )
        unpacked = e.unpack_wcx(packed)
        all_ok &= check("pack_wcx / unpack_wcx", bool(unpacked.success))
        all_ok &= check(
            "pack/unpack payload identity",
            bytes(unpacked.payload) == stream_payload,
        )
        dr3 = e.pipeline_decompress(
            bytes(unpacked.payload), [e.AlgorithmID.INFLATE], None, None, p3, None, 0
        )
        all_ok &= check("after pack_wcx decompress", bytes(dr3.data) == orig)

        Path(wcx_path).write_bytes(packed)
        dfr = e.pipeline_decompress_file(
            wcx_path, dec_path, [e.AlgorithmID.INFLATE], chunk, None, None, p3
        )
        dec_file = Path(dec_path).read_bytes() if Path(dec_path).is_file() else b""
        all_ok &= check(
            "pipeline_decompress_file",
            bool(getattr(dfr, "success", False)) and dec_file == orig,
            f"got {len(dec_file)} success={getattr(dfr, 'success', None)}",
        )

        comp = deflate_compressor(e, use_3hfmtree=True)
        dr4 = comp.decompress(stream_payload)
        all_ok &= check(
            "DeflateCompressor.decompress(3HfMT payload)",
            bytes(dr4.data) == orig,
            f"got {len(dr4.data)}",
        )

        # DEFLATE chain id (if used) must also roundtrip with deflate params
        dr5 = e.pipeline_decompress(
            stream_payload, [e.AlgorithmID.DEFLATE], None, None, p3, None, 0
        )
        all_ok &= check(
            "pipeline_decompress(DEFLATE id + deflate params)",
            bytes(dr5.data) == orig,
            f"got {len(dr5.data)}",
        )
    finally:
        import shutil

        shutil.rmtree(td, ignore_errors=True)

    print("---")
    print("OVERALL:", "PASS" if all_ok else "FAIL")
    return 0 if all_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())

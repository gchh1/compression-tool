#!/usr/bin/env python3
"""DPFlate round-trip consistency: memory / stream compress, WCX pack, pipeline decompress."""

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


def gui_dpflate_params(engine) -> object:
    """Mirror Package/config webcompress_settings.json dpflate section."""
    p = engine.DpflatePipelineParams()
    p.search_size = 32767
    p.lookahead_size = 255
    p.min_match = 0
    p.dp_sub_match_max = 6
    p.max_chain_length = 256
    p.match_engine = 1
    p.use_flag_encoding = True
    p.use_3hfmtree = False
    p.huffman_offset_chunk_bits = 8
    p.huffman_length_chunk_bits = 8
    return p


def compressor_params(engine) -> object:
    """Build params via DPFlateCompressor setters (GUI one-shot path)."""
    c = engine.DPFlateCompressor()
    c.set_search_size(32767)
    c.set_lookahead_size(255)
    c.set_min_match(0)
    c.set_dp_sub_match_max(6)
    c.set_match_engine(1)
    c.set_use_flag_encoding(True)
    c.set_use_3hfmtree(False)
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
    p = gui_dpflate_params(e)
    chunk = max(65536, int(args.chunk_kb) * 1024)
    all_ok = True

    print(f"corpus: {src.name} ({len(orig)} bytes) chunk={chunk}")

    # 1) Memory pipeline compress + decompress
    cr = e.pipeline_compress(bytes(orig), [e.AlgorithmID.DPFLATE], None, p, None, None, 0)
    mem_payload = bytes(cr.data)
    dr = e.pipeline_decompress(mem_payload, [e.AlgorithmID.DPFLATE], None, p, None, None, 0)
    dec_mem = bytes(dr.data)
    all_ok &= check(
        "memory pipeline roundtrip",
        dec_mem == orig,
        f"got {len(dec_mem)} expected {len(orig)}",
    )

    # 2) Stream file compress (raw payload in staged wcx)
    td = tempfile.mkdtemp(prefix="dpflate_wcx_")
    try:
        inp = os.path.join(td, "in.bin")
        wcx_path = os.path.join(td, "out.wcx")
        dec_path = os.path.join(td, "out.bin")
        Path(inp).write_bytes(orig)

        fr = e.pipeline_compress_file(
            inp, wcx_path, [e.AlgorithmID.DPFLATE], chunk, 0, None, p, None, None
        )
        all_ok &= check("stream compressFile success", bool(getattr(fr, "success", False)))
        stream_payload = wcx_payload(Path(wcx_path).read_bytes())
        same_payload = stream_payload == mem_payload
        if not same_payload:
            print(
                f"[INFO] stream vs memory payload differ: "
                f"stream={len(stream_payload)} mem={len(mem_payload)} (roundtrip still required)"
            )

        # 3) WCX full file: unpack + pipeline_decompress
        dr2 = e.pipeline_decompress(stream_payload, [e.AlgorithmID.DPFLATE], None, p, None, None, 0)
        all_ok &= check(
            "pipeline_decompress(WCX payload)",
            bytes(dr2.data) == orig,
            f"got {len(dr2.data)}",
        )

        # 4) pack_wcx roundtrip (GUI export path)
        packed = bytes(
            e.pack_wcx(stream_payload, e.AlgorithmID.DPFLATE, len(orig), src.name, False, False)
        )
        unpacked = e.unpack_wcx(packed)
        all_ok &= check("pack_wcx / unpack_wcx", bool(unpacked.success))
        inner = bytes(unpacked.payload)
        all_ok &= check("pack/unpack payload identity", inner == stream_payload)
        dr3 = e.pipeline_decompress(inner, [e.AlgorithmID.DPFLATE], None, p, None, None, 0)
        all_ok &= check("after pack_wcx decompress", bytes(dr3.data) == orig)

        # 5) pipeline_decompress_file (native WCX path)
        Path(wcx_path).write_bytes(packed)
        dfr = e.pipeline_decompress_file(
            wcx_path, dec_path, [e.AlgorithmID.DPFLATE], chunk, None, p, None
        )
        dec_file = Path(dec_path).read_bytes() if Path(dec_path).is_file() else b""
        all_ok &= check(
            "pipeline_decompress_file",
            bool(getattr(dfr, "success", False)) and dec_file == orig,
            f"got {len(dec_file)} success={getattr(dfr, 'success', None)}",
        )

        # 6) DPFlateCompressor (GUI one-shot) on stream payload
        comp = compressor_params(e)
        dr4 = comp.decompress(stream_payload)
        all_ok &= check(
            "DPFlateCompressor.decompress(stream payload)",
            bytes(dr4.data) == orig,
            f"got {len(dr4.data)}",
        )

        # 7) Wrong chain INFLATE must fail or not match (regression)
        dr_bad = e.pipeline_decompress(stream_payload, [e.AlgorithmID.INFLATE], None, p, None, None, 0)
        all_ok &= check(
            "INFLATE chain != DPFlate payload",
            bytes(dr_bad.data) != orig,
            "decompress must not use Deflate decoder on DPFlate WCX",
        )
    finally:
        import shutil

        shutil.rmtree(td, ignore_errors=True)

    print("---")
    print("OVERALL:", "PASS" if all_ok else "FAIL")
    return 0 if all_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())

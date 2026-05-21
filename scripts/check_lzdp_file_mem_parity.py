#!/usr/bin/env python3
"""Compare LZDP memory compress vs pipeline_compress_file with whole-file flag."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "Package" / "bin" / "core_engine"))
import core_engine as ce  # noqa: E402

FILE_COMPRESS_LZDP_WHOLE_FILE = 1

def main() -> int:
    p = ROOT / "resources" / "imdb-movie-reviews-word2vec-tfidf-bow.ipynb"
    data = p.read_bytes()
    wf = ce.LzdpWholeFileParams()
    wf.search_size = 4095
    wf.lookahead_size = 31
    wf.min_match = 0
    wf.dp_top = 6
    wf.use_flag_encoding = 1
    wf.match_engine = 1

    comp = ce.LZDPCompressor()
    comp.set_search_size(4095)
    comp.set_lookahead_size(31)
    comp.set_min_match(0)
    comp.set_dp_top(6)
    comp.set_use_flag_encoding(True)
    comp.set_match_engine(1)
    mem = comp.compress(data)

    ws = ROOT / "test_lzdp_flag_ws"
    ws.mkdir(exist_ok=True)
    inp = ws / "in.bin"
    out = ws / "out.wcx"
    inp.write_bytes(data)
    r = ce.pipeline_compress_file(
        str(inp),
        str(out),
        [ce.AlgorithmID.LZDP],
        512 * 1024,
        FILE_COMPRESS_LZDP_WHOLE_FILE,
        wf,
        None,
        None,
        None,
    )
    unpacked = ce.unpack_wcx(out.read_bytes())
    if not unpacked.success:
        print("unpack_wcx failed:", unpacked.error_message)
        return 1
    payload = bytes(unpacked.payload)
    mem_bytes = bytes(mem.data)
    print(f"memory payload: {len(mem_bytes)}")
    print(f"file payload:   {len(payload)}")
    print(f"match: {mem_bytes == payload}")
    return 0 if mem_bytes == payload else 1


if __name__ == "__main__":
    raise SystemExit(main())

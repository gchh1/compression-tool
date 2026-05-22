#!/usr/bin/env python3
"""
Generate raw binary/text-ish payloads for compression benchmarking.

For each target size, writes three kinds (compressibility profiles):
  - repetitive : single repeated byte (very compressible)
  - random     : deterministic PRNG stream (poorly compressible)
  - text       : repeated English/HTML-like snippet (typical web-ish)

Output: resources/fixtures/uncompressed/benchmark/
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import random

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.normpath(os.path.join(SCRIPT_DIR, ".."))
DEFAULT_OUT = os.path.join(
    REPO_ROOT, "resources", "fixtures", "uncompressed", "benchmark"
)

TEXT_SEED = (
    "<!-- bench --><p>Compression fixture: lorem ipsum dolor sit amet, "
    "consectetur adipiscing elit. Tags &amp; entities 42.</p>\n"
    "function tick(n){return n*3+1;} // synthetic js-ish line\n"
)

KINDS = ("repetitive", "random", "text")


def human_size(n: int) -> str:
    if n % (1024 * 1024) == 0 and n >= 1024 * 1024:
        return f"{n // (1024 * 1024)}mib"
    if n % 1024 == 0 and n >= 1024:
        return f"{n // 1024}kib"
    return f"{n}b"


def build_repetitive(n: int) -> bytes:
    return bytes([0x5A]) * n


def build_random(n: int, seed: int) -> bytes:
    rng = random.Random(seed)
    if hasattr(rng, "randbytes"):
        return rng.randbytes(n)
    return bytes(rng.randint(0, 255) for _ in range(n))


def build_text(n: int) -> bytes:
    raw = TEXT_SEED.encode("utf-8")
    if n == 0:
        return b""
    q, r = divmod(n, len(raw))
    return raw * q + raw[:r]


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--out",
        default=DEFAULT_OUT,
        help="Output directory (default: resources/fixtures/uncompressed/benchmark)",
    )
    parser.add_argument(
        "--sizes",
        default="4096,65536,1048576",
        help="Comma-separated exact byte sizes (default: 4KiB,64KiB,1MiB)",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=20260513,
        help="Seed for 'random' kind (default: 20260513)",
    )
    args = parser.parse_args()

    sizes = []
    for part in args.sizes.split(","):
        part = part.strip().lower().replace("_", "")
        if part.endswith("kib"):
            sizes.append(int(part[:-3]) * 1024)
        elif part.endswith("mib"):
            sizes.append(int(part[:-3]) * 1024 * 1024)
        elif part.endswith("kb"):
            sizes.append(int(part[:-2]) * 1000)
        elif part.endswith("mb"):
            sizes.append(int(part[:-2]) * 1000 * 1000)
        else:
            sizes.append(int(part))
    if any(s < 0 for s in sizes):
        raise SystemExit("sizes must be non-negative")

    os.makedirs(args.out, exist_ok=True)

    manifest = {
        "description": "Synthetic payloads: per size, three compressibility kinds.",
        "kinds": {
            "repetitive": "single byte 0x5A repeated",
            "random": f"deterministic PRNG (seed={args.seed})",
            "text": "UTF-8 English/HTML-like snippet tiled to length",
        },
        "files": [],
    }

    for n in sizes:
        label = human_size(n)
        for kind in KINDS:
            if kind == "repetitive":
                data = build_repetitive(n)
            elif kind == "random":
                data = build_random(n, args.seed ^ n)
            else:
                data = build_text(n)

            name = f"compress_test_{label}_{kind}.bin"
            path = os.path.join(args.out, name)
            with open(path, "wb") as f:
                f.write(data)
            rel = os.path.relpath(path, REPO_ROOT).replace("\\", "/")
            manifest["files"].append(
                {
                    "path": rel,
                    "bytes": n,
                    "kind": kind,
                    "sha256": sha256_bytes(data),
                }
            )
            print(f"wrote {rel} ({n} bytes, {kind})")

    manifest_path = os.path.join(args.out, "manifest.json")
    with open(manifest_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
        f.write("\n")
    mrel = os.path.relpath(manifest_path, REPO_ROOT).replace("\\", "/")
    print(f"wrote {mrel}")


if __name__ == "__main__":
    main()

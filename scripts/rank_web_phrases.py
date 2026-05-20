#!/usr/bin/env python3
"""Rank / sort web dictionary phrases for developers (corpus stats + canonical match order).

Runtime ``encode()`` uses **greedy longest match** with phrases ordered by
``web_dict.phrase_match_sort_key``: byte length descending, then UTF-8 string.

This tool helps tune ``resources/dict/*.txt`` before ``embed_web_dict.py``:

  1. Scan a corpus and score each phrase (hits, bytes saved vs 3-byte wire token).
  2. Emit recommended line order (length desc, then savings desc, then alpha).
  3. Optionally ``--write`` sorted phrases back into kind-specific txt files.

Examples::

  python scripts/rank_web_phrases.py --corpus tests/data/webpage_large --kind html
  python scripts/rank_web_phrases.py --corpus Package/samples -r --write
  python scripts/rank_web_phrases.py --kind all --report-only
"""

from __future__ import annotations

import argparse
import sys
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "src"))

from gui.engine.web_dict import (  # noqa: E402
    WebDictKind,
    _MIN_PHRASE_LEN,
    _WIRE_TOKEN_SIZE,
    phrase_match_sort_key,
    resolve_dict_kind,
    sort_phrases_for_matching,
)

SRC_DICT = ROOT / "resources" / "dict"
SHARED_FILE = "shared_phrases.txt"
KIND_FILES = {
    "html": ("HTML", "web_phrases_html.txt"),
    "css": ("CSS", "web_phrases_css.txt"),
    "js": ("JS", "web_phrases_js.txt"),
    "json": ("JSON", "web_phrases_json.txt"),
}

_CORPUS_SUFFIXES = {
    WebDictKind.HTML: {".html", ".htm"},
    WebDictKind.CSS: {".css"},
    WebDictKind.JS: {".js", ".mjs"},
    WebDictKind.JSON: {".json"},
}


@dataclass
class PhraseStats:
    phrase: str
    length: int
    hits: int = 0
    bytes_saved: int = 0

    @property
    def net_per_hit(self) -> int:
        return self.length - _WIRE_TOKEN_SIZE


def _parse_txt(path: Path) -> list[str]:
    out: list[str] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        s = raw.strip()
        if not s or s.startswith("#"):
            continue
        if len(s.encode("utf-8")) < _MIN_PHRASE_LEN:
            continue
        out.append(s)
    return out


def _load_phrase_set(kind: WebDictKind) -> list[str]:
    _, filename = KIND_FILES[kind.name.lower()]
    shared = _parse_txt(SRC_DICT / SHARED_FILE) if (SRC_DICT / SHARED_FILE).is_file() else []
    kind_lines = _parse_txt(SRC_DICT / filename)
    seen: set[str] = set()
    merged: list[str] = []
    for s in kind_lines + shared:
        if s in seen:
            continue
        seen.add(s)
        merged.append(s)
    return merged


def _collect_corpus_files(corpus: Path, kind: WebDictKind) -> list[Path]:
    suffixes = _CORPUS_SUFFIXES[kind]
    if corpus.is_file():
        return [corpus] if corpus.suffix.lower() in suffixes else []
    files: list[Path] = []
    for p in corpus.rglob("*"):
        if p.is_file() and p.suffix.lower() in suffixes:
            files.append(p)
    return sorted(files)


def _greedy_scan_stats(data: bytes, phrases: list[str]) -> dict[str, PhraseStats]:
    ordered = sort_phrases_for_matching(phrases)
    phrase_bytes = [(s, s.encode("utf-8")) for s in ordered]
    stats: dict[str, PhraseStats] = {
        s: PhraseStats(phrase=s, length=len(b)) for s, b in phrase_bytes
    }
    i = 0
    n = len(data)
    esc = 0xFF
    while i < n:
        if data[i] == esc:
            i += 1
            continue
        matched = False
        for s, pb in phrase_bytes:
            plen = len(pb)
            if plen and i + plen <= n and data[i : i + plen] == pb:
                st = stats[s]
                st.hits += 1
                st.bytes_saved += max(0, plen - _WIRE_TOKEN_SIZE)
                i += plen
                matched = True
                break
        if not matched:
            i += 1
    return stats


def _rank_key(st: PhraseStats) -> tuple:
    """Developer sort: match order keys + corpus savings as secondary."""
    return (
        -st.length,
        -st.bytes_saved,
        -st.hits,
        st.phrase,
    )


def _simulate_encoded_size(data: bytes, phrases: list[str]) -> int:
    ordered = sort_phrases_for_matching(phrases)
    phrase_bytes = [s.encode("utf-8") for s in ordered]
    out = 0
    i = 0
    n = len(data)
    esc = 0xFF
    while i < n:
        if data[i] == esc:
            out += 3
            i += 1
            continue
        matched = False
        for pb in phrase_bytes:
            plen = len(pb)
            if plen and i + plen <= n and data[i : i + plen] == pb:
                out += _WIRE_TOKEN_SIZE
                i += plen
                matched = True
                break
        if not matched:
            out += 1
            i += 1
    return out


def _write_phrase_file(path: Path, header_lines: list[str], phrases: list[str]) -> None:
    body = "\n".join(header_lines + [""] + phrases) + "\n"
    path.write_text(body, encoding="utf-8", newline="\n")


def _process_kind(
    kind: WebDictKind,
    corpus: Path | None,
    *,
    write: bool,
    report_only: bool,
    prune_zero: bool,
    top: int | None,
) -> int:
    phrases = _load_phrase_set(kind)
    if not phrases:
        print(f"[{kind.name}] no phrases loaded", file=sys.stderr)
        return 1

    files: list[Path] = []
    if corpus is not None:
        files = _collect_corpus_files(corpus, kind)
        if not files:
            print(f"[{kind.name}] no corpus files under {corpus}", file=sys.stderr)

    agg: dict[str, PhraseStats] = {s: PhraseStats(phrase=s, length=len(s.encode("utf-8"))) for s in phrases}
    total_raw = 0
    for fp in files:
        try:
            raw = fp.read_bytes()
        except OSError as e:
            print(f"  skip {fp}: {e}", file=sys.stderr)
            continue
        total_raw += len(raw)
        for s, st in _greedy_scan_stats(raw, phrases).items():
            a = agg[s]
            a.hits += st.hits
            a.bytes_saved += st.bytes_saved

    ranked = sorted(agg.values(), key=_rank_key)
    if prune_zero and files:
        ranked = [st for st in ranked if st.hits > 0]
    if top is not None and top > 0:
        ranked = ranked[:top]

    match_order = sort_phrases_for_matching(phrases)

    print(f"\n=== {kind.name} ({len(phrases)} phrases, corpus files={len(files)}, raw={total_raw:,} B) ===")
    print(f"{'phrase':<40} {'len':>4} {'hits':>8} {'saved':>10} {'net/hit':>7}")
    print("-" * 75)
    for st in ranked[: min(40, len(ranked))]:
        disp = st.phrase if len(st.phrase) <= 38 else st.phrase[:35] + "..."
        print(f"{disp!r:<40} {st.length:>4} {st.hits:>8} {st.bytes_saved:>10} {st.net_per_hit:>7}")
    if len(ranked) > 40:
        print(f"  ... and {len(ranked) - 40} more")

    if files and total_raw > 0:
        combined = bytearray()
        for fp in files:
            combined.extend(fp.read_bytes())
        enc_size = _simulate_encoded_size(bytes(combined), match_order)
        print(
            f"Simulated dict-layer size: {enc_size:,} B / {len(combined):,} B raw "
            f"({100.0 * enc_size / len(combined):.2f}%)"
        )

    if report_only or not write:
        return 0

    _, filename = KIND_FILES[kind.name.lower()]
    path = SRC_DICT / filename
    old_header: list[str] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        if raw.strip().startswith("#") or not raw.strip():
            old_header.append(raw)
        else:
            break
    if not old_header:
        old_header = [f"# {kind.name} phrases — sorted by rank_web_phrases.py", "#"]

    kind_only = _parse_txt(path)
    kind_set = set(kind_only)
    # Ranked phrases that belong to this file, then any leftovers (0-hit) in match order.
    ordered_kind: list[str] = []
    seen: set[str] = set()
    for st in ranked:
        if st.phrase in kind_set and st.phrase not in seen:
            ordered_kind.append(st.phrase)
            seen.add(st.phrase)
    for p in match_order:
        if p in kind_set and p not in seen:
            ordered_kind.append(p)
            seen.add(p)

    _write_phrase_file(path, old_header, ordered_kind)
    print(f"Wrote {path} ({len(ordered_kind)} lines)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Rank web dictionary phrases using a corpus")
    parser.add_argument(
        "--corpus",
        type=Path,
        help="File or directory of sample .html/.css/.js/.json files",
    )
    parser.add_argument(
        "--kind",
        choices=["html", "css", "js", "json", "all"],
        default="all",
    )
    parser.add_argument(
        "--write",
        action="store_true",
        help="Rewrite kind-specific txt files with recommended phrase order",
    )
    parser.add_argument(
        "--report-only",
        "-r",
        action="store_true",
        help="Print stats only (default if --write not set)",
    )
    parser.add_argument(
        "--prune-zero",
        action="store_true",
        help="When writing, drop phrases with zero corpus hits (dangerous if corpus is small)",
    )
    parser.add_argument(
        "--top",
        type=int,
        default=0,
        help="Keep only top N ranked phrases when writing (0 = all)",
    )
    args = parser.parse_args()

    kinds = list(WebDictKind) if args.kind == "all" else [WebDictKind[args.kind.upper()]]
    corpus = args.corpus
    if corpus is None:
        print("No --corpus: listing canonical match order only (no hit stats).", file=sys.stderr)

    rc = 0
    for kind in kinds:
        if _process_kind(
            kind,
            corpus,
            write=args.write,
            report_only=args.report_only or not args.write,
            prune_zero=args.prune_zero,
            top=args.top or None,
        ):
            rc = 1
    if not args.write:
        print("\nNext: edit resources/dict/*.txt, then:  python scripts/embed_web_dict.py")
    return rc


if __name__ == "__main__":
    raise SystemExit(main())

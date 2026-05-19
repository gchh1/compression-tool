#!/usr/bin/env python3
"""Print runtime web phrase -> code mapping (same order as ``web_dict.load_phrases``)."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "src"))

from gui.engine.web_dict import load_phrases  # noqa: E402

_MIN = 3


def main() -> None:
    load_phrases.cache_clear()
    phrases = load_phrases()
    print(f"# phrases={len(phrases)}  wire=[0xFF][code u16 LE]  literal 0xFF -> code 0")
    print(f"# {'code':>5}  {'bytes':>5}  phrase")
    print("#" + "-" * 60)
    for idx, phrase in enumerate(phrases):
        code = idx + 1
        text = phrase.decode("utf-8", errors="replace")
        print(f"{code:5d}  {len(phrase):5d}  {text!r}")


if __name__ == "__main__":
    main()

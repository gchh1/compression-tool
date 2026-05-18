#!/usr/bin/env python3
"""Step-by-step trace: algorithm_new/KMP.hpp vs algorithm/KMPMatcher.hpp"""

from __future__ import annotations


def build_next(pattern: list[int]) -> list[int]:
    n = len(pattern)
    nxt = [0] * n
    j = 0
    for i in range(1, n):
        while pattern[i] != pattern[j] and j != 0:
            j = nxt[j - 1]
        if pattern[i] == pattern[j]:
            j += 1
        nxt[i] = j
    return nxt


class TopMatch:
    def __init__(self, cap: int) -> None:
        self.cap = cap
        self.items: list[tuple[int, int]] = []

    def insert(self, off: int, length: int) -> None:
        if self.cap == 0 or off == 0:
            return
        lo, hi = 0, len(self.items)
        while lo < hi:
            mid = (lo + hi) // 2
            if self.items[mid][1] >= length:
                lo = mid + 1
            else:
                hi = mid
        self.items.insert(lo, (off, length))
        if len(self.items) > self.cap:
            self.items.pop()


def kmp_new_exact(search: bytes, look: bytes, dp_top: int = 3, min_match: int = 3):
    """Exact mirror of KMP.hpp — may raise IndexError when j==len(look)."""
    sl, ll = len(search), len(look)
    nxt = build_next(list(look))
    j = 0
    tm = TopMatch(dp_top)
    for i in range(sl + ll - 1):
        cur = search[i] if i < sl else look[i - sl]
        while cur != look[j] and j != 0:  # noqa: no UB guard — same as C++
            j = nxt[j - 1]
        if cur == look[j]:
            j += 1
        ml = min(j, ll)
        if ml >= min_match:
            off = sl - i + ml - 1
            tm.insert(off, ml)
        if j >= ll:
            j = nxt[ll - 1] if ll > 1 else 0
    return tm.items if tm.items else []


def kmp_old(search: bytes, look: bytes, dp_top: int = 3, min_match: int = 3) -> list[tuple[int, int]]:
    sl, ll = len(search), len(look)
    if ll < min_match:
        return []
    nxt = build_next(list(look))
    j = 0
    maxlen, mark_idx = 0, 0
    ring: list[tuple[int, int]] = []

    def try_insert(off: int, length: int) -> None:
        if off == 0:
            return
        if len(ring) < dp_top:
            ring.append((off, length))
            return
        for k in range(dp_top):
            if length > ring[k][1]:
                ring[k] = (off, length)
                return

    for i in range(sl + ll - 1):
        cur = search[i] if i < sl else look[i - sl]
        while cur != look[j] and j != 0:
            j = nxt[j - 1]
        if cur == look[j]:
            j += 1
        if j >= ll:
            ml = ll
            if ml >= min_match:
                dist = sl - i + ml - 1
                if dist > 0:
                    if dp_top > 1:
                        try_insert(dist, ml)
                    else:
                        return [(dist, ml)]
            j = nxt[ll - 1] if ll > 1 else 0
        elif j >= min_match:
            dist = sl - i + j - 1
            if dist > 0:
                if dp_top > 1:
                    try_insert(dist, j)
                elif j >= maxlen:
                    maxlen, mark_idx = j, i
    if dp_top == 1:
        if maxlen >= min_match:
            dist = sl - mark_idx + maxlen - 1
            if dist > 0:
                return [(dist, maxlen)]
        return []
    return sorted(ring, key=lambda x: -x[1])


def trace_new_steps(search: bytes, look: bytes, min_match: int = 3) -> None:
    sl, ll = len(search), len(look)
    nxt = build_next(list(look))
    print(f"  next({look!r}) = {nxt}")
    j = 0
    for i in range(sl + ll - 1):
        cur = search[i] if i < sl else look[i - sl]
        jb = j
        oob = ""
        if j >= ll:
            oob = "  *** j>=ll BEFORE while: C++ reads look[j] out of bounds"
        try:
            while cur != look[j] and j != 0:
                j = nxt[j - 1]
            matched = cur == look[j]
            if matched:
                j += 1
        except IndexError:
            oob = "  *** IndexError in while/if (same as C++ UB)"
            matched = False
        ml = min(j, ll)
        off = sl - i + ml - 1
        ins = (off, ml) if ml >= min_match and off > 0 else None
        if j >= ll:
            j = nxt[ll - 1] if ll > 1 else 0
        print(
            f"  i={i:2d} '{chr(cur)}'  j {jb:2d}->{j:2d}  "
            f"match_len={ml} offset={off:2d}{oob}"
            + (f"  -> insert{ins}" if ins else "")
        )


def run_case(name: str, search: bytes, look: bytes, dp_top: int = 3) -> None:
    print(f"\n{'='*64}\n{name}")
    print(f"  search={search!r}  look={look!r}  dp_top={dp_top}  min_match=3")
    print("\n[NEW] step trace (KMP.hpp):")
    trace_new_steps(search, look)
    try:
        new_r = kmp_new_exact(search, look, dp_top)
        new_ok = "OK"
    except IndexError as e:
        new_r = None
        new_ok = f"CRASH/UB: {e}"
    old_r = kmp_old(search, look, dp_top)
    print(f"\n  NEW final: {new_r}  ({new_ok})")
    print(f"  OLD final: {old_r}")
    if new_r is not None:
        print(f"  Same: {new_r == old_r}")


def main() -> None:
    run_case("A full match search=abc look=abc", b"abc", b"abc")
    run_case("B run search=a look=aa", b"a", b"aa")
    run_case("C empty search look=abc", b"", b"abc")
    run_case("D no match", b"xyz", b"abc")
    run_case("E dp_top=1 search=ababa look=aba", b"ababa", b"aba", dp_top=1)

    print(f"\n{'='*64}\nF fallback")
    print("  NEW (no match):", end=" ")
    try:
        print(kmp_new_exact(b"xyz", b"qrs"))
    except IndexError:
        print("CRASH")
    print("  OLD (no match):", kmp_old(b"xyz", b"qrs"))


if __name__ == "__main__":
    main()

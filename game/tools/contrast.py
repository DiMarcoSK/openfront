#!/usr/bin/env python3
"""Contrast gate (B05; spec-01 T023, SDD-5, constitution 7, WCAG 2.2 AA).

Reads the project tokens (tokens.json, W3C DTCG) and checks the 8 pairs
the visual contract locks:

    ink/paper 7.0, ink/surface 7.0, ink-2/paper 4.5,
    on-accent/accent 4.5, accent/paper 3.0,
    ok/paper 4.5, warn/paper 4.5, crit/paper 4.5

Fails with a readable report when any pair is under its minimum.
"""
from __future__ import annotations

import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOKENS = ROOT / "cases" / "nintendo-ds-strategy-tycoon-incremental" / \
    "tokens.json"

PAIRS = [
    ("ink", "paper", 7.0),
    ("ink", "surface", 7.0),
    ("ink-2", "paper", 4.5),
    ("on-accent", "accent", 4.5),
    ("accent", "paper", 3.0),
    ("ok", "paper", 4.5),
    ("warn", "paper", 4.5),
    ("crit", "paper", 4.5),
]


def srgb_channel(c: float) -> float:
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def luminance(hexstr: str) -> float:
    h = hexstr.lstrip("#")
    r, g, b = (int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4))
    lin = [srgb_channel(v) for v in (r, g, b)]
    return 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2]


def ratio(fg: float, bg: float) -> float:
    hi, lo = max(fg, bg), min(fg, bg)
    return (hi + 0.05) / (lo + 0.05)


def main() -> int:
    tokens = json.loads(TOKENS.read_text())
    colors = {k: v["$value"]["hex"] for k, v in tokens["color"].items()}
    print("contrast (WCAG 2.2, tokens.json -> visual-contract pairs):")
    failures = []
    ok_count = 0
    for fg, bg, minimum in PAIRS:
        r = ratio(luminance(colors[fg]), luminance(colors[bg]))
        verdict = "PASS" if abs(r) >= minimum else "FAIL"
        print(f"  {fg}/{bg:<10} {r:5.2f} >= {minimum}  {verdict}")
        if abs(r) >= minimum:
            ok_count += 1
        else:
            failures.append(f"{fg}/{bg} = {r:.2f} < {minimum}")
    if failures:
        print("\n".join(f"- {f}" for f in failures))
        print(f"contrast: {len(failures)} violation(s).")
        return 1
    print(f"contrast: {ok_count}/8 pairs pass.")
    return 0


if __name__ == "__main__":
    sys.exit(main())

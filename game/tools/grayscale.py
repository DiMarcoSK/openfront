#!/usr/bin/env python3
"""Grayscale fixture for the tier-band states (B17, spec-03 T023).

The token colours ok/warn/crit do NOT separate between states in pure
grayscale (ok/warn delta 0.0100) - by design, colour is never the only
carrier (constitution 7). What grayscale must guarantee is:

  1. every state colour stays readable on paper (>= 0.30 luminance
     contrast in grayscale), AND
  2. the state-vs-state distinction rides on the pin SILHOUETTE
     (solid / dash / cross), locked by tests/host/test_tier.c.

This tool locks fixture (1); collapsing the on-paper numbers fails.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

MIN_ON_PAPER = 0.30  # grayscale luminance contrast vs paper, per state


def lin(c: float) -> float:
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def luminance(hexstr: str) -> float:
    v = hexstr.lstrip("#")
    r, g, b = (int(v[i : i + 2], 16) / 255.0 for i in (0, 2, 4))
    return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b)


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    tokens = json.loads(
        (root.parent / "cases" / "nintendo-ds-strategy-tycoon-incremental"
         / "tokens.json").read_text()
    )
    colors = {k: d["$value"]["hex"] for k, d in tokens["color"].items()}
    states = ["ok", "warn", "crit"]
    lum = {s: luminance(colors[s]) for s in states}
    paper = luminance(colors["paper"])
    print("grayscale fixture (relative luminance, no chroma):")
    print(f"  paper {paper:.4f}")
    ok = True
    for s in states:
        d = paper - lum[s]
        print(f"  {s:5s} {lum[s]:.4f}  (on paper {d:.4f})")
        if d < MIN_ON_PAPER:
            print(f"    < {MIN_ON_PAPER} on paper: FAIL")
            ok = False
    print("  state-vs-state deltas (colour alone):")
    for i, a in enumerate(states):
        for b in states[i + 1:]:
            d = abs(lum[a] - lum[b])
            tag = "SHARED-SHAPE GUARD REQUIRED" if d < 0.05 else ""
            print(f"    {a}/{b:5s}{d:.4f}  {tag}")
    print("grayscale: FIXTURE HOLDS (state carried by silhouette)"
          if ok else "grayscale: COLLAPSE")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())

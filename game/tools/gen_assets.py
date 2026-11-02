#!/usr/bin/env python3
"""Placeholder assets for B01 (proper pixel art ships with B04/B07/B11/B30)."""
from PIL import Image
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "data"

def save(img, name):
    p = OUT / name
    img.save(p)
    print(f"wrote {p.name} {img.size}")

# 4bpp atlas: 16x16 tiles of 8 distinct colors + transparent.
ATLAS_COLOR = [(0,0,0,0), (237,230,208,255),(31,36,28,255),(79,84,69,255),
               (201,191,164,255),(46,90,39,255),(122,74,0,255),(140,27,21,255)]
atlas = Image.new("P", (16, 16), 0)
atlas.putpalette([c for rgba in ATLAS_COLOR for c in rgba])
px = atlas.load()
for i in range(16):
    for j in range(16):
        px[i, j] = (i + j) % (len(ATLAS_COLOR) - 1) + 1
save(atlas, "atlas.png")

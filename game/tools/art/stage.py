"""stage.py - shared helpers for the 256x192 boss stages (sky, clouds, hair).

Each stage is the boss itself painted as a background layer (the DS way to
put a colossus on screen: BG tiles, not OAM). The army fights on the bottom
band of the same picture, so the god towers over 16 px soldiers. Palette
animation (eyes, lightning, souls, sand) is listed per boss in CYCLES and
costs zero VRAM at runtime.
"""

from __future__ import annotations

import math

import numpy as np

from px import Canvas, ramp, noise, lightning

W, H = 256, 192
YY, XX = np.mgrid[0:H, 0:W]


def sky(cv, colors, y0=0, y1=H, dither=1.0):
    cv.gradient(cv.rect(0, 0, W - 1, H - 1), colors, y0, y1, dither)


def clouds(cv, seed, colors, y0, y1, thresh=0.52, scale=22.0, light=(-0.4, -0.9, 0.5),
           bevel=5.0, region=None, dither=0.6):
    n = noise(W, H, seed, scale, 4)
    fade = np.clip(1 - np.abs((YY - (y0 + y1) / 2) / ((y1 - y0) / 2)), 0, 1)
    m = (n * (0.55 + 0.6 * fade)) > thresh
    if region is not None:
        m &= region
    m &= (YY >= y0) & (YY <= y1)
    cv.shade(m, colors, light=light, bevel=bevel, dither=dither)
    return m


def soldiers_row(cv, y, x0, x1, step, colors, seed, spear=True, height=7):
    """Tiny silhouettes for scale: a phalanx line seen from far away."""
    rng = np.random.default_rng(seed)
    dark, mid = colors[0], colors[1]
    for x in range(x0, x1, step):
        h = height + int(rng.integers(-1, 2))
        cv.fill(cv.rect(x, y - h + 2, x + 1, y), dark)
        cv.put(x, y - h + 1, dark)
        cv.put(x + 1, y - h + 1, dark)
        cv.put(x + 2, y - h + 3, mid)
        if spear:
            cv.line(x + 1, y - h - 4, x + 1, y - h, mid)


def lock(cv, x0, y0, length, sway, width, seed, dx=0.0):
    """One flowing lock of hair or beard: a tapered S-curve polygon."""
    r = np.random.default_rng(seed)
    left, right = [], []
    ph = r.uniform(0, math.pi)
    n = max(3, int(length / 4))
    for i in range(n + 1):
        t = i / n
        x = x0 + math.sin(t * math.pi * 1.4 + ph) * sway * t + dx * t
        y = y0 + t * length
        w = width * (1 - t * 0.85) + 0.6
        left.append((x - w, y))
        right.append((x + w, y))
    return cv.poly(left + right[::-1])


def streak(cv, x0, y0, length, rise, bend, width):
    """A tapered horizontal lock (wind-swept hair, flame tongue, tentacle)."""
    top, bot = [], []
    for i in range(10):
        t = i / 9
        x = x0 + t * length
        y = y0 + t * rise - math.sin(t * math.pi) * bend
        w = width * (1 - t * 0.85) + 0.5
        top.append((x, y - w))
        bot.append((x, y + w))
    return cv.poly(top + bot[::-1])


def rim_light(cv, fig, color, toward=(1, -1), clip=None):
    """Light every edge of `fig` that faces the light source."""
    dx, dy = toward
    edge = np.zeros_like(fig)
    if dx:
        edge |= fig & ~np.roll(fig, -dx, axis=1)
    if dy:
        edge |= fig & ~np.roll(fig, -dy, axis=0)
    if clip is not None:
        edge &= clip
    cv.fill(edge, color)




def taper(cv, pts, wmax, ends=(0.0, 0.0)):
    """Polygon around a polyline, widest mid-way and pointed at the ends."""
    n = len(pts)
    seg = [math.hypot(pts[i + 1][0] - pts[i][0], pts[i + 1][1] - pts[i][1])
           for i in range(n - 1)]
    total = sum(seg) or 1.0
    acc, left, right = 0.0, [], []
    for i, (x, y) in enumerate(pts):
        a = pts[max(0, i - 1)]
        b = pts[min(n - 1, i + 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        ln = math.hypot(dx, dy) or 1.0
        nx, ny = -dy / ln, dx / ln
        t = acc / total
        w = wmax * max(math.sin(math.pi * t), 0) + ends[0] * (1 - t) + ends[1] * t
        left.append((x + nx * w, y + ny * w))
        right.append((x - nx * w, y - ny * w))
        if i < n - 1:
            acc += seg[i]
    return cv.poly(left + right[::-1])


def carve(cv, pts, color, mask=None, width=1):
    """A drawn line clipped to a form: anatomy grooves, strand gaps, cracks."""
    m = cv.stroke(pts, width)
    if mask is not None:
        m &= mask
    cv.fill(m, color)


def rain(cv, seed, color, count=90, length=6, slant=2, region=None):
    rng = np.random.default_rng(seed)
    for _ in range(count):
        x, y = rng.uniform(0, W), rng.uniform(0, H)
        m = cv.stroke([(x, y), (x - slant, y + length)], 1)
        if region is not None:
            m &= region
        cv.fill(m, color)


def bezier(p0, p1, p2, p3, n=24):
    out = []
    for i in range(n + 1):
        t = i / n
        u = 1 - t
        out.append((u ** 3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t ** 3 * p3[0],
                    u ** 3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t ** 3 * p3[1]))
    return out


def tube(cv, pts, w0, w1, side=None):
    """Polygon of a body along a path, width easing w0 -> w1.

    side=+1/-1 returns only that half (belly plates, dorsal ridge).
    """
    n = len(pts)
    left, right = [], []
    for i, (x, y) in enumerate(pts):
        a = pts[max(0, i - 1)]
        b = pts[min(n - 1, i + 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        ln = math.hypot(dx, dy) or 1.0
        nx, ny = -dy / ln, dx / ln
        w = w0 + (w1 - w0) * (i / (n - 1))
        lw = 0 if side == -1 else w
        rw = 0 if side == 1 else w
        left.append((x + nx * lw, y + ny * lw))
        right.append((x - nx * rw, y - ny * rw))
    return cv.poly(left + right[::-1])


def scales(cv, mask, color, step=5):
    """Offset rows of tiny arcs: reads as scale armour at DS size."""
    def draw(d):
        for row, y in enumerate(range(0, H, step - 1)):
            off = (step // 2) * (row % 2)
            for x in range(-step + off, W, step):
                d.line([(x, y), (x + 1, y + 1), (x + step - 2, y + 1), (x + step - 1, y)],
                       fill=255, width=1)
    cv.fill(cv._mask_from(draw) & mask, color)


def foam(cv, seed, color, region, density=0.6, scale=4.0):
    n = noise(W, H, seed, scale, 2)
    cv.fill(region & (n > density), color)

"""px.py - the pixel-art core every War Table art generator paints with.

Colours are snapped to the DS 15-bit space (5 bits per channel) on entry, so
what a preview shows is what the BGR555 palette RAM will hold. A Canvas is an
RGBA numpy array; shapes are boolean masks so shading, outlining and dithering
compose without anti-aliasing (nearest-neighbour only, BRANDBOOK section 6).
"""

from __future__ import annotations

import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

BAYER4 = np.array([[0, 8, 2, 10],
                   [12, 4, 14, 6],
                   [3, 11, 1, 9],
                   [15, 7, 13, 5]], dtype=np.float32) / 16.0 - 0.5


def ds(rgb) -> tuple:
    """Snap an RGB triple to what BGR555 palette RAM displays."""
    out = []
    for v in rgb[:3]:
        q = int(round(v * 31 / 255))
        out.append((q << 3) | (q >> 2))
    return tuple(out)


def hexc(code: str) -> tuple:
    code = code.lstrip("#")
    return ds(tuple(int(code[i:i + 2], 16) for i in (0, 2, 4)))


def bgr555(rgb) -> int:
    r, g, b = (int(round(v * 31 / 255)) for v in rgb[:3])
    return r | (g << 5) | (b << 10)


def mix(a, b, t: float) -> tuple:
    return ds(tuple(a[i] + (b[i] - a[i]) * t for i in range(3)))


def ramp(*codes) -> list:
    return [hexc(c) if isinstance(c, str) else ds(c) for c in codes]


class Canvas:
    def __init__(self, w: int, h: int, fill=None):
        self.w, self.h = w, h
        self.a = np.zeros((h, w, 4), dtype=np.uint8)
        if fill is not None:
            self.a[..., :3] = fill
            self.a[..., 3] = 255

    # -- masks ------------------------------------------------------------
    def _mask_from(self, fn) -> np.ndarray:
        img = Image.new("L", (self.w, self.h), 0)
        fn(ImageDraw.Draw(img))
        return np.array(img) > 127

    def poly(self, pts) -> np.ndarray:
        pts = [(float(x), float(y)) for x, y in pts]
        return self._mask_from(lambda d: d.polygon(pts, fill=255))

    def ellipse(self, cx, cy, rx, ry) -> np.ndarray:
        yy, xx = np.mgrid[0:self.h, 0:self.w]
        return ((xx + 0.5 - cx) / max(rx, 0.01)) ** 2 + \
               ((yy + 0.5 - cy) / max(ry, 0.01)) ** 2 <= 1.0

    def rect(self, x0, y0, x1, y1) -> np.ndarray:
        m = np.zeros((self.h, self.w), bool)
        x0, y0, x1, y1 = int(round(x0)), int(round(y0)), int(round(x1)), int(round(y1))
        m[max(0, y0):max(0, y1 + 1), max(0, x0):max(0, x1 + 1)] = True
        return m

    def stroke(self, pts, width=1) -> np.ndarray:
        pts = [(float(x), float(y)) for x, y in pts]
        return self._mask_from(lambda d: d.line(pts, fill=255, width=width))

    def opaque(self) -> np.ndarray:
        return self.a[..., 3] > 0

    # -- painting ---------------------------------------------------------
    def fill(self, mask, color):
        self.a[mask, :3] = color
        self.a[mask, 3] = 255

    def put(self, x, y, color):
        x, y = int(x), int(y)
        if 0 <= x < self.w and 0 <= y < self.h:
            self.a[y, x, :3] = color
            self.a[y, x, 3] = 255

    def get(self, x, y):
        if 0 <= x < self.w and 0 <= y < self.h and self.a[y, x, 3]:
            return tuple(int(v) for v in self.a[y, x, :3])
        return None

    def line(self, x0, y0, x1, y1, color):
        x0, y0, x1, y1 = int(round(x0)), int(round(y0)), int(round(x1)), int(round(y1))
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        err = dx + dy
        while True:
            self.put(x0, y0, color)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def erase(self, mask):
        self.a[mask] = 0

    def paste(self, other: "Canvas", x: int, y: int, flip=False):
        src = other.a[:, ::-1] if flip else other.a
        h, w = src.shape[:2]
        x0, y0 = max(0, x), max(0, y)
        x1, y1 = min(self.w, x + w), min(self.h, y + h)
        if x1 <= x0 or y1 <= y0:
            return
        s = src[y0 - y:y1 - y, x0 - x:x1 - x]
        m = s[..., 3] > 0
        self.a[y0:y1, x0:x1][m] = s[m]

    def sprite(self, rows, keys: dict, x: int, y: int, flip=False):
        """Stamp an ASCII sprite: each char maps to a colour in `keys`."""
        for j, row in enumerate(rows):
            for i, ch in enumerate(row):
                if ch in keys and keys[ch] is not None:
                    xi = x + (len(row) - 1 - i if flip else i)
                    self.put(xi, y + j, keys[ch])

    # -- shading ----------------------------------------------------------
    def shade(self, mask, colors, light=(-0.55, -0.75, 0.6), bevel=6.0,
              dither=0.9, bias=0.0, gain=1.0, ox=0, oy=0, ambient=None):
        """Sculpt a mask: distance-field height -> normal -> lit ramp step.

        The ramp runs dark->light. Bayer dithering only bands the boundary
        between two steps, so forms read round without gradients. With
        `ambient` set the light is Lambert (hard chiaroscuro: planes that
        turn away fall to the ambient step); without it, half-wrap (soft).
        """
        if not mask.any():
            return
        h = dist_inside(mask, int(bevel) + 2)
        hn = np.clip(h / bevel, 0, 1)
        hn = np.sqrt(1 - (1 - hn) ** 2) * bevel
        gy, gx = np.gradient(hn)
        lx, ly, lz = light
        ln = math.sqrt(lx * lx + ly * ly + lz * lz)
        nz = np.ones_like(gx)
        nn = np.sqrt(gx * gx + gy * gy + nz * nz)
        d = (-gx * lx - gy * ly + nz * lz) / (nn * ln)
        if ambient is None:
            v = np.clip((d * 0.5 + 0.5) * gain + bias, 0, 1)
        else:
            v = np.clip((ambient + (1 - ambient) * np.clip(d, 0, 1)) * gain + bias, 0, 1)
        self.tone(mask, colors, v, dither, ox, oy)

    def tone(self, mask, colors, v, dither=0.9, ox=0, oy=0):
        n = len(colors)
        yy, xx = np.mgrid[0:self.h, 0:self.w]
        b = BAYER4[(yy + oy) % 4, (xx + ox) % 4] * dither
        idx = np.clip(np.floor(v * (n - 1) + 0.5 + b), 0, n - 1).astype(int)
        for i, c in enumerate(colors):
            sel = mask & (idx == i)
            self.a[sel, :3] = c
            self.a[sel, 3] = 255

    def gradient(self, mask, colors, y0, y1, dither=1.0, x_axis=False):
        yy, xx = np.mgrid[0:self.h, 0:self.w]
        t = ((xx if x_axis else yy) - y0) / max(1, (y1 - y0))
        self.tone(mask, colors, np.clip(t, 0, 1), dither)

    def outline(self, color, where=None, diagonal=False, only_into=None):
        """1 px exterior outline around opaque pixels (4- or 8-connected)."""
        op = self.opaque() if where is None else where
        grown = dilate(op, diagonal)
        ring = grown & ~op
        if only_into is not None:
            ring &= only_into
        self.fill(ring, color)

    def inner_line(self, mask, color, side="bottom"):
        """Darken the edge pixels of a mask on one side (form shadow)."""
        sh = {"bottom": (1, 0), "top": (-1, 0), "right": (0, 1), "left": (0, -1)}[side]
        nb = np.roll(mask, (-sh[0], -sh[1]), axis=(0, 1))
        self.fill(mask & ~nb, color)

    def rim(self, mask, color, side=(-1, -1)):
        """Light the edge of a mask that faces the light."""
        dy, dx = side[1], side[0]
        nb = np.roll(np.roll(mask, dy, axis=0), dx, axis=1)
        self.fill(mask & ~nb, color)

    # -- io ---------------------------------------------------------------
    def image(self) -> Image.Image:
        return Image.fromarray(self.a, "RGBA")

    def save(self, path, scale=1):
        img = self.image()
        if scale != 1:
            img = img.resize((self.w * scale, self.h * scale), Image.NEAREST)
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        img.save(path)

    def colors(self) -> set:
        m = self.opaque()
        return {tuple(int(v) for v in c) for c in self.a[m][:, :3]}

    def crop(self, x, y, w, h) -> "Canvas":
        c = Canvas(w, h)
        c.a[:] = self.a[y:y + h, x:x + w]
        return c

    def copy(self) -> "Canvas":
        c = Canvas(self.w, self.h)
        c.a[:] = self.a
        return c

    def recolor(self, table: dict):
        for src, dst in table.items():
            m = np.all(self.a[..., :3] == src, axis=-1) & self.opaque()
            self.a[m, :3] = dst


def dilate(m: np.ndarray, diagonal=False) -> np.ndarray:
    out = m.copy()
    out[1:] |= m[:-1]
    out[:-1] |= m[1:]
    out[:, 1:] |= m[:, :-1]
    out[:, :-1] |= m[:, 1:]
    if diagonal:
        out[1:, 1:] |= m[:-1, :-1]
        out[1:, :-1] |= m[:-1, 1:]
        out[:-1, 1:] |= m[1:, :-1]
        out[:-1, :-1] |= m[1:, 1:]
    return out


def erode(m: np.ndarray) -> np.ndarray:
    return ~dilate(~m)


def dist_inside(m: np.ndarray, cap: int) -> np.ndarray:
    """Chamfer-ish distance to the mask edge, capped (enough for bevels)."""
    d = np.zeros(m.shape, np.float32)
    cur = m.copy()
    for i in range(cap):
        d += cur
        cur = erode(cur)
        if not cur.any():
            break
    return d


def noise(w, h, seed, scale=8.0, octaves=3) -> np.ndarray:
    """Deterministic value noise in [0,1] (clouds, smoke, water, stone)."""
    rng = np.random.default_rng(seed)
    out = np.zeros((h, w), np.float32)
    amp, tot = 1.0, 0.0
    for o in range(octaves):
        s = scale / (2 ** o)
        gw, gh = int(w / s) + 3, int(h / s) + 3
        g = rng.random((gh, gw)).astype(np.float32)
        yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
        fx, fy = xx / s, yy / s
        x0, y0 = fx.astype(int), fy.astype(int)
        tx, ty = fx - x0, fy - y0
        tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
        a = g[y0, x0] * (1 - tx) + g[y0, x0 + 1] * tx
        b = g[y0 + 1, x0] * (1 - tx) + g[y0 + 1, x0 + 1] * tx
        out += (a * (1 - ty) + b * ty) * amp
        tot += amp
        amp *= 0.5
    return out / tot


def lightning(cv: Canvas, x, y, length, angle, rng, color, glow=None,
              branch=0.35, depth=0, width=1):
    """Recursive fractal bolt; returns the main path end point."""
    pts = [(x, y)]
    seg = 7 if depth == 0 else 5
    for _ in range(int(length / seg)):
        angle += rng.uniform(-0.55, 0.55)
        x += math.cos(angle) * seg
        y += math.sin(angle) * seg
        pts.append((x, y))
        if depth < 2 and rng.random() < branch:
            lightning(cv, x, y, length * 0.45, angle + rng.choice([-0.8, 0.8]),
                      rng, color, glow, branch * 0.5, depth + 1, 1)
    if glow is not None:
        cv.fill(cv.stroke(pts, width + 2) & ~cv.stroke(pts, width), glow)
    cv.fill(cv.stroke(pts, width), color)
    return x, y

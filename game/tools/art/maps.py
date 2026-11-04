"""maps.py - 512x512 battle maps seen from the high camera.

A map is twice the screen in each axis; the battle camera scrolls over it.
It is built so the DS can afford it: everything a pixel depends on is the
four corner materials of its 16x16 metatile (Wang-corner autotiling) plus
textures that repeat every 16 px. Rivers, roads and cliffs still get organic
edges, but identical neighbourhoods produce identical 8x8 tiles, so the
flip-reduced tile count stays far under the 1024-tile BG limit.

Biomes: plains (river valley), desert (mesas and an oasis), highlands
(lake and pass) and the god arena, one map whose palette swaps per god.
"""

from __future__ import annotations

import math

import numpy as np

from px import Canvas, ramp, noise, dilate, BAYER4

N = 512
M = 16                 # metatile size
G = N // M + 1         # corner grid (33x33)
YY, XX = np.mgrid[0:N, 0:N]

GRASS, DIRT, WATER, ROCK, MEADOW = 0, 1, 2, 3, 4


def pnoise(seed, period=16, scale=4.0):
    """Noise that repeats every `period` px (tileable texture source)."""
    base = noise(period * 3, period * 3, seed, scale, 2)[period:2 * period, period:2 * period]
    # make it seamless by mirroring blend
    b = (base + base[::-1, :] + base[:, ::-1] + base[::-1, ::-1]) / 4
    return np.tile(b, (N // period, N // period))


def refine(grid, seed):
    """33x33 corners (16 px) -> 65x65 corners (8 px). In-between corners
    take one neighbour's material at random, so coastlines and cliff lines
    wander at 8 px instead of stepping at 16 px."""
    rng = np.random.default_rng(seed)
    g = np.full((2 * G - 1, 2 * G - 1), -1, int)
    g[::2, ::2] = grid
    for y in range(g.shape[0]):
        for x in range(g.shape[1]):
            if g[y, x] >= 0:
                continue
            nb = []
            for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1), (-1, -1), (1, 1), (-1, 1), (1, -1)):
                yy, xx = y + dy, x + dx
                if 0 <= yy < g.shape[0] and 0 <= xx < g.shape[1] and g[yy, xx] >= 0 and                         (yy % 2 == 0 and xx % 2 == 0):
                    nb.append(g[yy, xx])
            g[y, x] = nb[int(rng.integers(0, len(nb)))]
    return g


def corner_weight(grid, m):
    """Bilinear weight of material m from the corner grid, per pixel."""
    ind = (grid == m).astype(np.float32)
    step = N / (grid.shape[0] - 1)
    gx, gy = XX / step, YY / step
    x0, y0 = gx.astype(int), gy.astype(int)
    tx, ty = gx - x0, gy - y0
    # smoothstep keeps the edge curvy instead of diamond-shaped
    tx, ty = tx * tx * (3 - 2 * tx), ty * ty * (3 - 2 * ty)
    a = ind[y0, x0] * (1 - tx) + ind[y0, x0 + 1] * tx
    b = ind[y0 + 1, x0] * (1 - tx) + ind[y0 + 1, x0 + 1] * tx
    return a * (1 - ty) + b * ty


def region(grid, m, seed, amp=0.45):
    w = corner_weight(grid, m)
    return (w + (pnoise(seed) - 0.5) * amp) > 0.5


def tone(cv, mask, colors, v, dither=0.8):
    n = len(colors)
    b = BAYER4[YY % 4, XX % 4] * dither
    idx = np.clip(np.floor(v * (n - 1) + 0.5 + b), 0, n - 1).astype(int)
    for i, c in enumerate(colors):
        sel = mask & (idx == i)
        cv.a[sel, :3] = c
        cv.a[sel, 3] = 255


def paint_line(grid, pts, m, width=1):
    """Rasterise a polyline of corner coordinates into the grid."""
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        steps = int(max(abs(x1 - x0), abs(y1 - y0)) * 2) + 1
        for i in range(steps + 1):
            t = i / steps
            x, y = round(x0 + (x1 - x0) * t), round(y0 + (y1 - y0) * t)
            for dy in range(-width + 1, width):
                for dx in range(-width + 1, width):
                    if 0 <= x + dx < G and 0 <= y + dy < G:
                        grid[y + dy, x + dx] = m


def blob(grid, cx, cy, r, m, seed):
    rng = np.random.default_rng(seed)
    gy, gx = np.mgrid[0:G, 0:G]
    wob = 1 + 0.35 * np.sin(np.arctan2(gy - cy, gx - cx) * 3 + rng.uniform(0, 6))
    grid[np.hypot(gx - cx, gy - cy) < r * wob] = m


# ---------------------------------------------------------------------------
# Stamps: trees, rocks, ruins, buildings. Drawn at metatile positions only,
# so each variant lands on the same texture phase -> repeats as tiles.
# ---------------------------------------------------------------------------
def tree(cv, x, y, P, kind=0):
    """A canopy seen from high up: round crown, light top-left, shadow SE."""
    sh = cv.ellipse(x + 10, y + 12, 7, 4)
    cv.fill(sh, P["shadow"])
    r = 6 if kind != 2 else 5
    crown = cv.ellipse(x + 8, y + 7, r + 0.5, r)
    if kind == 1:
        crown |= cv.ellipse(x + 5, y + 9, 4, 4) | cv.ellipse(x + 11, y + 9, 4, 4)
    cv.shade(crown, P["leaf"], light=(-0.6, -0.7, 0.4), bevel=4, dither=0.5, ambient=0.15)
    if kind == 2:  # conifer: a star of needles
        for k in range(6):
            a = k * math.pi / 3
            cv.line(x + 8, y + 7, x + 8 + math.cos(a) * 5, y + 7 + math.sin(a) * 5, P["leaf"][0])
    cv.put(x + 6, y + 4, P["leaf"][-1])


def rock(cv, x, y, P, k=0):
    sh = cv.ellipse(x + 9, y + 11, 6, 3)
    cv.fill(sh, P["shadow"])
    rk = cv.poly([(x + 3, y + 10), (x + 5, y + 5), (x + 9, y + 4 - k), (x + 13, y + 7), (x + 12, y + 11)])
    cv.shade(rk, P["rock"], light=(-0.6, -0.7, 0.4), bevel=2, dither=0.4, ambient=0.2)


def column(cv, x, y, P, broken=False):
    h = 7 if broken else 12
    cv.fill(cv.rect(x + 7, y + 13 - h + 4, x + 12, y + 14), P["shadow"])
    cv.fill(cv.rect(x + 5, y + 13 - h, x + 9, y + 12), P["marble"][2])
    cv.fill(cv.rect(x + 5, y + 13 - h, x + 5, y + 12), P["marble"][3])
    cv.fill(cv.rect(x + 9, y + 13 - h, x + 9, y + 12), P["marble"][1])
    cv.fill(cv.rect(x + 4, y + 12, x + 10, y + 13), P["marble"][1])
    if not broken:
        cv.fill(cv.rect(x + 4, y + 13 - h - 1, x + 10, y + 13 - h), P["marble"][3])


def house(cv, x, y, P, big=False):
    w = 12 if big else 9
    cv.fill(cv.rect(x + 4, y + 6, x + 4 + w, y + 14), P["shadow"])
    cv.fill(cv.rect(x + 2, y + 6, x + 2 + w, y + 12), P["wall"][1])
    cv.fill(cv.rect(x + 2, y + 6, x + 2 + w, y + 6), P["wall"][2])
    roof = cv.poly([(x + 1, y + 7), (x + 2 + w // 2, y + 2), (x + 3 + w, y + 7)])
    cv.fill(roof, P["roof"][1])
    cv.fill(cv.poly([(x + 2 + w // 2, y + 2), (x + 3 + w, y + 7), (x + 2 + w // 2, y + 7)]), P["roof"][0])
    cv.put(x + 5, y + 9, P["wall"][0])


def field_rows(cv, mask, P):
    rows = mask & ((YY % 4) == 0)
    cv.fill(rows, P["dirt"][0])
    crop = mask & ((YY % 4) == 2) & ((XX % 3) != 0)
    cv.fill(crop, P["crop"])


# ---------------------------------------------------------------------------
# Biome palettes (the ramps every map paints with).
# ---------------------------------------------------------------------------
BIOMES = {
    "plains": dict(
        base=ramp("#2c4a1e", "#3a5e24", "#4a722c", "#5e8636", "#76a044"),
        dirt=ramp("#5a4026", "#76583a", "#937250", "#ae8e68"),
        water=ramp("#14324a", "#1c4a66", "#2a6884", "#4a8ea6", "#a8d8e0"),
        rock=ramp("#3a3836", "#55514a", "#73705f", "#97917c", "#bdb59c"),
        leaf=ramp("#152a12", "#1f3c18", "#2e5420", "#44702c", "#6a9440"),
        shadow=ramp("#1c3014")[0], crop=ramp("#c8b060")[0],
        marble=ramp("#6e6a5e", "#9a9480", "#cdc6ae", "#efe8d2"),
        wall=ramp("#8a7a5e", "#c4b08a", "#e6d6b0"), roof=ramp("#7a2a1a", "#a8442a"),
        cliff=ramp("#2a2622", "#3c3630")),
    "desert": dict(
        base=ramp("#a0703a", "#b8864a", "#cc9c5c", "#ddb474", "#ecca92"),
        dirt=ramp("#8a6a44", "#a4825a", "#bc9a70", "#d4b48a"),
        water=ramp("#123a40", "#1a5458", "#287070", "#4a9a8e", "#b6e0cc"),
        rock=ramp("#5a3420", "#7a4a2c", "#9a6038", "#bc7c48", "#d89c64"),
        leaf=ramp("#1c3014", "#2c4818", "#406420", "#5a8028", "#80a038"),
        shadow=ramp("#82562c")[0], crop=ramp("#6a8a30")[0],
        marble=ramp("#8a7458", "#b49c78", "#d8c4a0", "#f2e2c0"),
        wall=ramp("#9a7a54", "#c8a478", "#e8cca0"), roof=ramp("#6a4428", "#8e6038"),
        cliff=ramp("#3a1e12", "#5a3020")),
    "highlands": dict(
        base=ramp("#26402a", "#324f32", "#41613c", "#557448", "#6e8a58"),
        dirt=ramp("#4a4236", "#625846", "#7c705a", "#968a70"),
        water=ramp("#0e2638", "#16384e", "#22506a", "#3a7090", "#9cc8d8"),
        rock=ramp("#2e3238", "#40454c", "#565c63", "#70767c", "#d6dce2"),
        leaf=ramp("#0e2218", "#163022", "#20442c", "#2e5a38", "#467648"),
        shadow=ramp("#1a2c1c")[0], crop=ramp("#a8a060")[0],
        marble=ramp("#5e6066", "#86888e", "#b8bac0", "#e6e8ec"),
        wall=ramp("#6a6258", "#968c7c", "#c0b6a2"), roof=ramp("#3a3a44", "#56565e"),
        cliff=ramp("#22252c", "#33373f")),
}


def _paint_terrain(grid, P, seed, features):
    mrng = np.random.default_rng(seed + 9)
    for _ in range(7):
        cx, cy = mrng.uniform(2, G - 2, 2)
        sub = np.zeros_like(grid)
        blob(sub, cx, cy, mrng.uniform(2, 4), MEADOW, int(mrng.integers(0, 99)))
        grid[(sub == MEADOW) & (grid == GRASS)] = MEADOW
    grid = refine(grid, seed + 7)
    cv = Canvas(N, N)
    tex = pnoise(seed, 16, 4.0)
    tex2 = pnoise(seed + 1, 16, 2.0)
    tone(cv, np.ones((N, N), bool), P["base"][:4], np.clip(tex * 0.9 + tex2 * 0.4 - 0.15, 0, 1))
    tuft = (tex2 > 0.66) & ((XX + YY * 3) % 7 == 0)
    cv.fill(tuft, P["base"][4])

    meadow = region(grid, MEADOW, seed + 5, 0.8)
    tone(cv, meadow, P["base"][1:], np.clip(tex * 0.9 + tex2 * 0.3, 0, 1))
    cv.fill(meadow & (tex2 > 0.62) & ((XX * 7 + YY * 5) % 11 == 0), P["crop"])

    dirt = region(grid, DIRT, seed + 2)
    tone(cv, dirt, P["dirt"][:3], np.clip(tex * 0.8 + 0.1, 0, 1))
    cv.fill(dilate(dirt) & ~dirt, P["base"][0])
    if "fields" in features:
        field_rows(cv, dirt & features["fields"], P)

    rock = region(grid, ROCK, seed + 3, 0.3)
    top = rock & np.roll(rock, 5, axis=0)       # plateau top: shifted up 5 px
    face = rock & ~top
    tone(cv, top, P["rock"][1:], np.clip(tex2 * 0.9 + (tex - 0.5) * 0.4, 0, 1))
    cracks = top & (pnoise(seed + 6, 16, 1.5) > 0.68) & ((XX + YY) % 3 == 0)
    cv.fill(cracks, P["rock"][0])
    tone(cv, face, P["cliff"] + [P["rock"][0]], np.clip((YY % 6) / 6 * 0.6 + tex * 0.4, 0, 1))
    shadow = np.roll(rock, 3, axis=0) & np.roll(rock, 2, axis=1) & ~rock
    cv.fill(shadow & ~dirt, P["shadow"])
    cv.fill(top & ~np.roll(top, 1, axis=0), P["rock"][4])   # lit rim

    water = region(grid, WATER, seed + 4, 0.3)
    tone(cv, water, P["water"][:4], np.clip(tex * 0.5 + (tex2 > 0.6) * 0.5, 0, 1), 0.6)
    shore = dilate(water) & ~water
    cv.fill(shore, P["water"][4])
    cv.fill(dilate(shore) & ~shore & ~water & ~rock, P["dirt"][3])
    glint = water & (tex2 > 0.72) & ((XX % 5) == 0)
    cv.fill(glint, P["water"][4])
    return cv, dict(dirt=dirt, rock=rock, water=water)


_STAMPS = {}

# What an army cannot walk through, per map (lanes.py routes round it):
# rock and cliff, water and its shore, and every solid stamp. Trees are
# not in it - a wood is ground the lanes cross (GAME_DESIGN 7 forests).
SOLID = {}
_solid_stamps = np.zeros((N, N), bool)


def stamp(cv, fn, x, y, P, *args):
    """Draw a stamp once per (fn, args) on a 24x24 cell, then paste it."""
    key = (fn.__name__, id(P), args)
    if key not in _STAMPS:
        small = Canvas(24, 24)
        fn(small, 0, 0, P, *args)
        _STAMPS[key] = small
    cv.paste(_STAMPS[key], x, y)
    if fn is not tree:
        op = _STAMPS[key].opaque()
        h, w = min(24, N - y), min(24, N - x)
        _solid_stamps[y:y + h, x:x + w] |= op[:h, :w]


def _keep_solid(name, mk):
    global _solid_stamps
    water = mk["water"]
    SOLID[name] = mk["rock"] | water | dilate(water) | _solid_stamps
    _solid_stamps = np.zeros((N, N), bool)


def _stamp_cells(grid, masks, rng, density, avoid):
    cells = []
    for gy in range(N // M):
        for gx in range(N // M):
            x, y = gx * M, gy * M
            if avoid[y:y + M, x:x + M].any():
                continue
            if rng.random() < density(gx, gy):
                cells.append((x, y))
    return cells


def plains() -> Canvas:
    P = BIOMES["plains"]
    rng = np.random.default_rng(1)
    grid = np.zeros((G, G), int)
    # river winding north->south, a road crossing it at a ford
    river = [(20, 0), (19, 5), (22, 10), (18, 15), (14, 20), (16, 26), (13, 32)]
    paint_line(grid, river, WATER, 2)
    paint_line(grid, [(0, 17), (8, 16), (16, 17), (24, 15), (32, 16)], DIRT)
    blob(grid, 25, 26, 3.5, DIRT, 3)        # farm fields by the village
    blob(grid, 5, 5, 3, ROCK, 4)            # a rocky knoll
    blob(grid, 27, 4, 2.5, ROCK, 5)
    paint_line(grid, [(14, 16), (18, 16)], DIRT)   # the ford
    # two more fords where the north and south lanes wade the river:
    # an army crosses at the shallows, never over deep water
    paint_line(grid, [(17, 12), (25, 12)], DIRT, 2)
    paint_line(grid, [(10, 18), (19, 18)], DIRT, 2)
    cv, mk = _paint_terrain(grid, P, 10, {"fields": (XX > 340) & (YY > 360)})
    avoid = mk["water"] | mk["dirt"] | mk["rock"]
    forest = (noise(N // M, N // M, 77, 6.0, 2) > 0.58)
    cells = _stamp_cells(grid, mk, rng, lambda gx, gy: 0.85 if forest[gy, gx] else 0.04, avoid)
    for x, y in cells:
        j = int(rng.integers(0, 4))
        stamp(cv, tree, x + (j % 2) * 6 - 3, y + (j // 2) * 6 - 3, P, int(rng.integers(0, 2)))
    for x, y in ((400, 336), (432, 340), (416, 368), (448, 372), (384, 368)):
        stamp(cv, house, x, y, P, x == 432)
    for x, y in ((96, 48), (112, 48), (96, 80), (128, 64)):
        stamp(cv, rock, x, y, P, 1)
    _keep_solid("plains", mk)
    return cv


def desert() -> Canvas:
    P = BIOMES["desert"]
    rng = np.random.default_rng(2)
    grid = np.zeros((G, G), int)
    blob(grid, 8, 8, 5, ROCK, 11)           # mesas
    blob(grid, 27, 27, 4, ROCK, 12)      # south of the battle lanes
    blob(grid, 25, 6, 2.5, ROCK, 13)
    blob(grid, 10, 25, 2.6, WATER, 14)      # the oasis
    paint_line(grid, [(0, 14), (12, 15), (20, 13), (32, 18)], DIRT)   # caravan track
    cv, mk = _paint_terrain(grid, P, 20, {})
    # dunes: crescent ridges repeating on the 16 px phase
    ridge = ((YY + (XX // 16) % 2 * 8) % 32 == 0) & (pnoise(21) > 0.45)
    cv.fill(ridge & ~mk["rock"] & ~mk["water"] & ~mk["dirt"], P["base"][4])
    cv.fill(np.roll(ridge, 1, axis=0) & ~mk["rock"] & ~mk["water"] & ~mk["dirt"], P["base"][0])
    avoid = mk["water"] | mk["rock"] | mk["dirt"]
    oasis = np.zeros((N // M, N // M), bool)
    oasis[20:31, 5:16] = True
    cells = _stamp_cells(grid, mk, rng, lambda gx, gy: 0.6 if oasis[gy, gx] else 0.02, avoid)
    for x, y in cells:
        j = int(rng.integers(0, 4))
        stamp(cv, tree, x + (j % 2) * 6 - 3, y + (j // 2) * 6 - 3, P, 2 if oasis[y // M, x // M] else 0)
    for x, y in ((224, 96), (240, 96), (256, 112)):
        stamp(cv, column, x, y, P, x != 240)
    _keep_solid("desert", mk)
    return cv


def highlands() -> Canvas:
    P = BIOMES["highlands"]
    rng = np.random.default_rng(3)
    grid = np.zeros((G, G), int)
    grid[:, :4] = ROCK                      # mountain walls left and right
    grid[:, 29:] = ROCK
    blob(grid, 16, 7, 4, WATER, 21)         # tarn
    paint_line(grid, [(16, 32), (15, 24), (17, 16), (12, 12), (10, 0)], DIRT)  # the pass road
    blob(grid, 20, 24, 2.5, ROCK, 22)
    cv, mk = _paint_terrain(grid, P, 30, {})
    avoid = mk["water"] | mk["rock"] | mk["dirt"]
    forest = (noise(N // M, N // M, 78, 5.0, 2) > 0.5)
    cells = _stamp_cells(grid, mk, rng, lambda gx, gy: 0.8 if forest[gy, gx] else 0.05, avoid)
    for x, y in cells:
        j = int(rng.integers(0, 4))
        stamp(cv, tree, x + (j % 2) * 6 - 3, y + (j // 2) * 6 - 3, P, 2)
    for x, y in ((240, 400), (256, 400), (272, 416)):
        stamp(cv, house, x, y, P, False)
    _keep_solid("highlands", mk)
    return cv


# ---------------------------------------------------------------------------
# The god arena: one map, five palettes. Marble plaza, broken colonnade,
# and a chasm that is lava for Ares, the sea for Poseidon, souls for Hades,
# storm for Zeus, the starfield for Kronos.
# ---------------------------------------------------------------------------
ARENA_BASE = dict(
    base=ramp("#30302e", "#3c3c38", "#4a4a44", "#5a5a52", "#6c6c62"),
    dirt=ramp("#6e6a5e", "#8a8676", "#a6a290", "#c4bfaa"),
    water=ramp("#202020", "#383838", "#585858", "#808080", "#c0c0c0"),
    rock=ramp("#282826", "#3e3e3a", "#56564f", "#727068", "#94928a"),
    leaf=ramp("#1a1a18", "#2a2a26", "#3a3a34", "#4c4c44", "#60605a"),
    shadow=ramp("#202020")[0], crop=ramp("#909090")[0],
    marble=ramp("#5e5a50", "#8a8476", "#bcb6a4", "#e8e2ce"),
    wall=ramp("#6a6458", "#948c7c", "#bdb4a0"), roof=ramp("#3a1a14", "#5a2a1e"),
    cliff=ramp("#141414", "#222222"),
)

# per-god recolour of the arena ramps (same slots, new colours)
ARENA_GODS = {
    "zeus": dict(base=ramp("#141c30", "#1c2640", "#26324e", "#32405e", "#42526e"),
                 water=ramp("#0c1430", "#1a2c5c", "#2f6bd8", "#6fd8ff", "#ffffff"),
                 dirt=ramp("#4a5878", "#66769a", "#8696b8", "#b4c4dc")),
    "poseidon": dict(base=ramp("#14282a", "#1a3436", "#224244", "#2c5252", "#386462"),
                     water=ramp("#031018", "#0b2830", "#185050", "#2ad8c0", "#e8f8f2"),
                     dirt=ramp("#5a6a5a", "#7a8c78", "#9cae96", "#c2d0b6")),
    "hades": dict(base=ramp("#0e0a14", "#160f1e", "#1e152a", "#281c36", "#342444"),
                  water=ramp("#06120e", "#0f3a33", "#1d6e5a", "#3aae86", "#d4ffe8"),
                  dirt=ramp("#3a3442", "#524a5a", "#6c6274", "#8a8090")),
    "ares": dict(base=ramp("#1a0c0a", "#26120e", "#341a12", "#442216", "#562c1a"),
                 water=ramp("#3a0a04", "#7a1a08", "#d84a10", "#ff9a2a", "#ffe680"),
                 dirt=ramp("#6a4a2a", "#8c643a", "#ae824c", "#d0a464")),
    "kronos": dict(base=ramp("#0a0614", "#120a24", "#1c1034", "#261646", "#321e58"),
                   water=ramp("#04020a", "#1c1034", "#6a2468", "#d6ccff", "#ffffff"),
                   dirt=ramp("#5a4a3a", "#7a6648", "#a0885a", "#d0b878")),
}


def arena() -> Canvas:
    P = ARENA_BASE
    rng = np.random.default_rng(4)
    grid = np.zeros((G, G), int)
    blob(grid, 16, 16, 9, DIRT, 41)        # the plaza
    blob(grid, 16, 25, 2.4, ROCK, 42)      # the altar dais, south of the lanes
    grid[:3, :] = WATER                    # the chasm frames the arena
    grid[-3:, :] = WATER
    grid[:, :2] = WATER
    grid[:, -2:] = WATER
    paint_line(grid, [(2, 25), (7, 22)], WATER)
    paint_line(grid, [(30, 8), (25, 10)], WATER)
    cv, mk = _paint_terrain(grid, P, 40, {})
    plaza = mk["dirt"]
    slabs = plaza & (((XX % 16) == 0) | ((YY % 16) == 0))
    cv.fill(slabs, P["dirt"][0])
    crack = plaza & (pnoise(43, 16, 2.0) > 0.7) & ((XX + YY) % 2 == 0)
    cv.fill(crack, P["base"][0])
    ring = [(int(256 + math.cos(a) * 150) // 16 * 16, int(256 + math.sin(a) * 150) // 16 * 16)
            for a in np.linspace(0, 2 * math.pi, 14, endpoint=False)]
    for i, (x, y) in enumerate(ring):
        stamp(cv, column, x, y, P, i % 3 == 1)
    for x, y in ((96, 400), (400, 96), (112, 112), (384, 400)):
        stamp(cv, rock, x, y, P, 2)
    _keep_solid("arena", mk)
    return cv


def arena_palette(god: str, pal: list) -> list:
    """Recolour the arena palette for one god, slot by slot."""
    swap = {}
    for key, ramp_new in ARENA_GODS[god].items():
        for old, new in zip(ARENA_BASE[key], ramp_new):
            swap[tuple(old)] = tuple(new)
    return [swap.get(tuple(c), tuple(c)) for c in pal]


MAPS = {"plains": plains, "desert": desert, "highlands": highlands, "arena": arena}

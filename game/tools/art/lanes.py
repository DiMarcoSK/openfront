"""lanes.py - where the three battle lanes may walk on each 512x512 map.

The sim (src/sim/field.c) moves divisions along x in 3 lanes; it never
sees the map. This file is the bridge: for every map it finds, per lane,
a smooth path of y offsets (one per 8 px column) whose feet band never
touches rock, cliff, water or a solid stamp (columns, houses, boulders),
plus the x span the lane can walk at all (a mountain wall or the arena
chasm ends it). render.c reads the table (art_terrain.h) and the battle
view places every squad, corpse and general on it.

The search is a Viterbi pass over 64 columns x 41 offsets: an offset
costs its distance from the lane's nominal row, a change between columns
costs its size, and a slope over 4 px per 8 px column is not allowed, so
a lane bends round an obstacle instead of teleporting past it. The lanes
are solved centre first, then north and south, each kept >= LANE_GAP px
clear of the one already placed: they never merge or cross.
"""

from __future__ import annotations

import numpy as np

N = 512
COL = 8                       # px per column
COLS = N // COL               # 64
LANE_Y = (186, 228, 270)      # battle.c LANE_Y (sprite top row of a squad)
FEET = (3, 30)                # feet band below LANE_Y (+ jitter, + slack)
DYS = np.arange(-40, 41, 2)   # candidate offsets
MAX_STEP = 4                  # px of y per column
LANE_GAP = 26                 # min px between two lanes' rows
INF = 1 << 30


def free_table(solid: np.ndarray, lane: int) -> np.ndarray:
    """free[c, k]: the feet band of offset DYS[k] is clear in column c."""
    free = np.zeros((COLS, len(DYS)), bool)
    for k, dy in enumerate(DYS):
        y0 = LANE_Y[lane] + dy + FEET[0]
        y1 = LANE_Y[lane] + dy + FEET[1]
        if y0 < 0 or y1 > N:
            continue
        band = solid[y0:y1]
        free[:, k] = ~band.reshape(y1 - y0, COLS, COL).any(axis=(0, 2))
    return free


def span(free: np.ndarray) -> tuple:
    """The contiguous run of walkable columns that holds the clash (x 256)."""
    ok = free.any(axis=1)
    mid = COLS // 2
    if not ok[mid]:
        raise ValueError("the centre of the field is blocked for a lane")
    a = mid
    while a > 0 and ok[a - 1]:
        a -= 1
    b = mid
    while b < COLS - 1 and ok[b + 1]:
        b += 1
    return a, b


def solve(free: np.ndarray, a: int, b: int, ban=None) -> np.ndarray:
    """Viterbi over columns a..b; returns dy per column (edges held flat)."""
    K = len(DYS)
    cost = np.where(free, np.abs(DYS)[None, :], INF).astype(np.int64)
    if ban is not None:
        cost = np.where(ban, INF, cost)
    best = np.full(K, INF, np.int64)
    best[:] = cost[a]
    back = np.zeros((COLS, K), np.int64)
    steps = MAX_STEP // 2     # DYS step is 2 px
    for c in range(a + 1, b + 1):
        nxt = np.full(K, INF, np.int64)
        for k in range(K):
            if cost[c, k] >= INF:
                continue
            lo, hi = max(0, k - steps), min(K, k + steps + 1)
            prev = best[lo:hi] + 3 * np.abs(np.arange(lo, hi) - k)
            j = int(np.argmin(prev))
            if prev[j] >= INF:
                continue
            nxt[k] = prev[j] + cost[c, k]
            back[c, k] = lo + j
        best = nxt
    k = int(np.argmin(best))
    if best[k] >= INF:
        raise ValueError("no walkable lane path")
    out = np.zeros(COLS, np.int64)
    for c in range(b, a - 1, -1):
        out[c] = DYS[k]
        k = back[c, k]
    out[:a] = out[a]
    out[b + 1:] = out[b]
    return out


def lanes(solid: np.ndarray) -> tuple:
    """(dy[3][64], span[3] as (x0, x1) px) for one map."""
    dy = [None] * 3
    spans = [None] * 3
    order = (1, 0, 2)
    for lane in order:
        free = free_table(solid, lane)
        ban = None
        for other in order:
            if dy[other] is None:
                continue
            # keep LANE_GAP clear of every lane already laid
            row = LANE_Y[other] + dy[other]          # per column
            mine = LANE_Y[lane] + DYS[None, :]       # per offset
            near = np.abs(mine - row[:, None]) < LANE_GAP
            if lane < other:
                near |= mine > row[:, None]
            else:
                near |= mine < row[:, None]
            ban = near if ban is None else ban | near
        a, b = span(free if ban is None else free & ~ban)
        dy[lane] = solve(free, a, b, ban)
        spans[lane] = (a * COL + COL // 2, b * COL + COL // 2)
    return dy, spans


def preview(cv, solid, dy, spans):
    """The map with its solid ground tinted red and the three lanes drawn."""
    from px import Canvas
    out = Canvas(N, N)
    out.a[:] = cv.a
    tint = out.a[solid]
    tint[:, 0] = np.minimum(255, tint[:, 0] // 2 + 128)
    tint[:, 1] //= 2
    tint[:, 2] //= 2
    out.a[solid] = tint
    for lane in range(3):
        x0, x1 = spans[lane]
        for x in range(x0, x1):
            y = LANE_Y[lane] + int(dy[lane][x // COL])
            for f in (FEET[0], FEET[1] - 1):
                out.a[y + f, x, :3] = (255, 255, 0) if lane == 1 else (0, 255, 255)
                out.a[y + f, x, 3] = 255
    return out

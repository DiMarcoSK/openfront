"""boss_kronos.py - KRONOS, the Devourer of Ages.

Time has no body, so Kronos borrows the sky's. The hooded titan is a cut-out
of the void, stars showing through him; an ancient stone mask with two
dying suns for eyes, golden orrery rings ticking round him (in front and
behind), the harpe sickle he used on Ouranos, and an hourglass whose sand
pours onto the battlefield, freezing ruins in mid-air. Sand, eyes and the
ring ticks are palette-cycled.
"""

import math

import numpy as np

from px import Canvas, ramp, noise, dilate
from stage import W, H, YY, XX, sky, soldiers_row, rim_light, taper, carve, bezier, tube

PAL = dict(
    void=ramp("#04020a", "#0a0616", "#120a24", "#1c1034", "#2a1848"),
    nebula=ramp("#3a1450", "#6a2468", "#a2407a", "#d8709a"),
    star=ramp("#8a7cc8", "#d6ccff", "#ffffff"),
    gold=ramp("#2e1c08", "#6a4614", "#b07e28", "#e8be58", "#fff0b0"),
    stone=ramp("#120e16", "#2a2430", "#4a4250", "#746a76", "#a69a9e", "#d6cac2"),
    sun=ramp("#c2400c", "#ff8a1c", "#ffd24a", "#ffffff"),
    sand=ramp("#7a4e1a", "#c08a34", "#f0c460", "#fff0b4"),
    ground=ramp("#030206", "#0a0612", "#140c20", "#20142e"),
)

CYCLES = {"sand": ["sand"], "eyes": ["sun"], "ticks": ["gold"]}


def ring(cv, cx, cy, rx, ry, tilt, width):
    """Mask of an elliptic ring plus a front/back split by y."""
    ca, sa = math.cos(tilt), math.sin(tilt)
    x, y = XX + 0.5 - cx, YY + 0.5 - cy
    u, v = x * ca + y * sa, -x * sa + y * ca
    d = (u / rx) ** 2 + (v / ry) ** 2
    band = (d <= 1.0) & (d >= (1 - width / min(rx, ry)) ** 2)
    front = v > 0
    return band, front, (u, v)


def paint() -> Canvas:
    P = PAL
    V, Ne, Sr, G, St, Su, Sa = P["void"], P["nebula"], P["star"], P["gold"], P["stone"], P["sun"], P["sand"]
    cv = Canvas(W, H)
    sky(cv, V[::-1], 0, 160)
    nb = noise(W, H, 121, 26.0, 4)
    neb = nb > 0.56
    cv.tone(neb, [V[3], Ne[0], Ne[1], Ne[2]], np.clip((nb - 0.56) * 5, 0, 1), 1.0)
    rng = np.random.default_rng(5)
    for _ in range(140):
        cv.put(rng.uniform(0, W), rng.uniform(0, 150), Sr[int(rng.integers(0, 3))])
    L = (0.0, -0.4, 0.6)

    cx, my = 128, 58
    # Back halves of the orrery rings (behind the titan).
    rings = [(cx, 112, 122, 26, -0.14, 4), (cx, 104, 96, 40, 0.36, 3)]
    ring_masks = [ring(cv, *r) for r in rings]
    for band, front, _ in ring_masks:
        cv.fill(band & ~front, G[1])

    # The hooded titan: a silhouette cut out of a deeper void, stars inside.
    hood = cv.ellipse(cx, my - 6, 48, 50)
    rag = noise(W, H, 133, 7.0, 1)
    mantle = cv.poly([(cx - 48, 50), (cx + 48, 50), (cx + 96, 120), (cx + 124, 160), (cx - 124, 160),
                      (cx - 96, 120)])
    for k in range(9):
        x = cx - 116 + k * 29 + (k % 2) * 6
        mantle &= ~cv.poly([(x - 7, 161), (x + 7, 161), (x + 1, 146 - (k % 3) * 6)])
    body = hood | mantle
    inner = noise(W, H, 131, 9.0, 3)
    cv.tone(body, [V[0], V[1], V[2]], np.clip(inner * 1.3 - 0.35, 0, 1), 1.0)
    rng = np.random.default_rng(9)
    for _ in range(90):
        x, y = rng.uniform(0, W), rng.uniform(0, 160)
        if body[int(y), int(x)]:
            cv.put(x, y, Sr[int(rng.integers(0, 3))])
    # A spiral galaxy in his chest where a heart would be.
    for k in range(260):
        t = k / 260 * 4 * math.pi
        rr = 8 + t * 2.6
        for arm in (0, math.pi):
            x = cx + math.cos(t + arm) * rr
            y = 124 + math.sin(t + arm) * rr * 0.45
            if body[int(y), int(x)]:
                cv.put(x, y, Ne[3] if k % 3 else Sr[2])
    cv.fill(cv.ellipse(cx, 124, 4, 2.4), Sr[2])
    hem = dilate(body, True) & ~body
    cv.fill(hem, G[2])
    cv.fill(dilate(hem) & ~hem & ~body, V[0])

    # The stone mask: ancient, cracked, two dying suns for eyes.
    mask = cv.ellipse(cx, my, 25, 28) & ~cv.rect(0, my + 22, W, H)
    mask |= cv.poly([(cx - 22, my + 8), (cx + 22, my + 8), (cx + 26, my + 30), (cx + 16, my + 52),
                     (cx, my + 62), (cx - 16, my + 52), (cx - 26, my + 30)])
    cv.fill(dilate(mask, True) & ~mask, V[0])
    cv.shade(mask, St, light=L, bevel=8, dither=0.4, ambient=0.12)
    brow = cv.poly([(cx - 21, my - 8), (cx, my - 4), (cx + 21, my - 8), (cx + 20, my - 3), (cx - 20, my - 3)])
    cv.shade(brow, St[1:], light=L, bevel=2, ambient=0.3)
    for side in (-1, 1):
        ex, ey = cx + side * 10, my + 1
        cv.fill(cv.ellipse(ex, ey, 7, 5), St[0])
        cv.fill(cv.ellipse(ex, ey, 4.6, 4.6), Su[0])
        cv.fill(cv.ellipse(ex, ey, 3.2, 3.2), Su[1])
        cv.fill(cv.ellipse(ex, ey, 1.8, 1.8), Su[2])
        cv.put(ex, ey, Su[3])
        for a in range(0, 360, 45):
            fx = ex + math.cos(math.radians(a)) * 6.5
            fy = ey + math.sin(math.radians(a)) * 6.5
            if not mask[int(fy), int(fx)] or True:
                cv.put(fx, fy, Su[0])
    nose = cv.poly([(cx - 2, my + 2), (cx + 2, my + 2), (cx + 4, my + 14), (cx - 4, my + 14)])
    cv.shade(nose, St[2:], light=L, bevel=2, ambient=0.3)
    mouth = cv.poly([(cx - 10, my + 19), (cx + 10, my + 19), (cx + 7, my + 23), (cx - 7, my + 23)])
    cv.fill(mouth, St[0])
    cv.fill(cv.rect(cx - 8, my + 20, cx + 8, my + 20), Su[0])
    for k in range(9):
        t = (k - 4) / 4
        carve(cv, [(cx + t * 18, my + 24), (cx + t * 20 + math.sin(k) * 2, my + 38),
                   (cx + t * 12, my + 56)], St[1] if k % 2 else St[0], mask)
        carve(cv, [(cx + t * 18 + 2, my + 26), (cx + t * 19 + 2, my + 34)], St[4], mask)
    # Cracks of age, one glowing with the light behind the mask.
    carve(cv, [(cx + 6, my - 27), (cx + 9, my - 16), (cx + 5, my - 9), (cx + 8, my - 5)], Su[1], mask)
    carve(cv, [(cx - 4, my + 30), (cx - 1, my + 42), (cx - 5, my + 54), (cx - 2, my + 60)], Su[1], mask)
    carve(cv, [(cx - 14, my + 8), (cx - 17, my + 16), (cx - 13, my + 24)], St[0], mask)
    carve(cv, [(cx + 15, my + 10), (cx + 19, my + 18)], St[0], mask)
    # Laurel-of-ages: a broken gold diadem.
    for i in range(-5, 6):
        if i in (2, 3):
            continue
        t = i / 5
        cv.shade(cv.ellipse(cx + t * 20, my - 12 - (1 - t * t) * 4, 2.6, 1.6), G, light=L, bevel=1,
                 ambient=0.3)

    # The hourglass, held in the void of his chest.
    hx, hy = cx, 124
    glass = cv.poly([(hx - 14, hy - 18), (hx + 14, hy - 18), (hx + 2, hy), (hx + 14, hy + 18),
                     (hx - 14, hy + 18), (hx - 2, hy)])
    cv.fill(dilate(glass, True) & ~glass, V[0])
    cv.fill(glass, V[3])
    cv.fill(glass & (YY > hy + 8), Sa[2])
    cv.fill(glass & (YY > hy + 8) & (YY < hy + 11), Sa[3])
    cv.fill(glass & (YY < hy - 8) & (YY > hy - 15), Sa[1])
    cv.fill(cv.rect(hx, hy - 3, hx, hy + 12), Sa[3])
    cv.fill(cv.stroke([(hx - 10, hy - 15), (hx - 3, hy - 4)], 1) & glass, Sr[1])
    for yb in (hy - 21, hy + 18):
        cv.shade(cv.rect(hx - 17, yb, hx + 17, yb + 3), G, light=L, bevel=1, ambient=0.3)
    for xb in (hx - 17, hx + 15):
        cv.shade(cv.rect(xb, hy - 18, xb + 2, hy + 18), G, light=L, bevel=1, ambient=0.3)

    # The harpe: a moon-sized sickle sweeping in from the right.
    hand = cv.ellipse(cx + 70, 96, 9, 8)
    arm = tube(cv, [(cx + 40, 70), (cx + 58, 86), (cx + 70, 96)], 9, 7) & ~hood
    cv.tone(arm, [V[1], V[2]], np.clip(inner, 0, 1), 1.0)
    blade_o = bezier((cx + 72, 92), (cx + 140, 40), (cx + 110, -30), (cx + 28, -6), 30)
    blade_i = bezier((cx + 28, -6), (cx + 92, -12), (cx + 112, 38), (cx + 74, 86), 30)
    blade = cv.poly(blade_o + blade_i)
    cv.fill(dilate(blade, True) & ~blade, V[0])
    cv.shade(blade, St, light=(0.5, -0.6, 0.4), bevel=4, dither=0.4, ambient=0.15)
    edge = cv.stroke(blade_i, 2) & blade
    cv.fill(edge, Su[2])
    cv.fill(cv.stroke(blade_i, 1) & blade, Su[3])
    for k in range(5):
        p = blade_o[6 + k * 4]
        cv.fill(cv.ellipse(p[0] - 4, p[1] + 2, 1.4, 1.4), G[3])
    haft = tube(cv, [(cx + 62, 116), (cx + 78, 82)], 3, 3)
    cv.shade(haft, G, light=L, bevel=1, ambient=0.3)
    cv.tone(hand, [V[1], V[2], V[3]], np.clip(inner + 0.2, 0, 1), 1.0)
    cv.fill(dilate(hand) & ~hand & ~blade & ~haft, G[2])

    # Front halves of the rings, with clock ticks.
    for (band, front, (u, v)), (rcx, rcy, rx, ry, tilt, wd) in zip(ring_masks, rings):
        cv.fill(band & front, G[2])
        ang = np.arctan2(v / ry, u / rx)
        tick = band & front & (np.mod(ang * 12 / math.pi, 1.0) < 0.18)
        cv.fill(tick, G[4])
        cv.fill(band & front & (np.roll(band, 1, axis=0) == 0), G[3])

    # Rim of starlight on the titan.
    rim_light(cv, hood, G[3], (0, -1), clip=~mask)

    # Ground: cracked earth, ruins frozen mid-air, sand pouring down.
    cv.gradient(YY >= 160, P["ground"][::-1], 160, 192, 1.0)
    cv.fill(cv.poly([(0, 164), (60, 160), (120, 163), (190, 159), (256, 162), (256, 166), (0, 168)]),
            P["ground"][3])
    for k in range(7):
        x = 18 + k * 38
        carve(cv, [(x, 168), (x + 6, 176), (x + 2, 184), (x + 9, 192)], Su[0])
    stream = cv.rect(hx - 1, hy + 21, hx + 1, 165)
    cv.tone(stream, Sa, np.clip(noise(W, H, 141, 2.0, 1) + 0.2, 0, 1), 1.0)
    pile = cv.ellipse(hx, 166, 16, 4)
    cv.shade(pile, Sa, light=L, bevel=2, ambient=0.4)
    rocks = [(30, 132, 9, 6), (62, 146, 5, 4), (196, 138, 8, 5), (226, 120, 6, 4), (176, 150, 4, 3)]
    for x, y, rx, ry in rocks:
        rk = cv.poly([(x - rx, y), (x - rx // 2, y - ry), (x + rx, y - ry + 1), (x + rx - 1, y + ry),
                      (x - rx // 2, y + ry)])
        cv.fill(dilate(rk, True) & ~rk, V[0])
        cv.shade(rk, St[:5], light=L, bevel=2, ambient=0.2)
        cv.fill(cv.rect(x - rx + 2, y + ry + 3, x + rx - 2, y + ry + 3), Sa[0])
    col = cv.poly([(40, 100), (52, 96), (60, 124), (48, 128)])
    cv.shade(col, St, light=L, bevel=3, ambient=0.2)
    for k in range(3):
        carve(cv, [(43 + k * 4, 99 - k), (51 + k * 4, 126 - k)], St[1], col)
    soldiers_row(cv, 180, 20, 110, 5, (P["ground"][0], St[3]), 13)
    soldiers_row(cv, 180, 150, 240, 5, (P["ground"][0], St[3]), 14)
    soldiers_row(cv, 190, 6, 250, 6, (P["ground"][0], St[2]), 15, height=9)
    return cv

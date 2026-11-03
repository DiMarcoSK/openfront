"""boss_zeus.py - ZEUS, the Storm Throne.

A colossus of storm-marble whose beard and hair are the weather itself. He
rises out of the cloud bank behind the field with the keraunos raised: the
whole figure is lit only by the bolt (upper right), so the body sits in deep
shadow with an electric rim. Phase 2 floods the sky (CYCLES flash).
"""

import math

import numpy as np

from px import Canvas, ramp, noise, lightning, dilate
from stage import W, H, YY, XX, sky, clouds, soldiers_row, lock, streak, rim_light, \
    taper, carve, rain

PAL = dict(
    sky=ramp("#060818", "#0b112a", "#121d3e", "#1a2c52", "#243f68"),
    cloud=ramp("#0c1022", "#18203c", "#263457", "#3c5278", "#62799e", "#9cb0cc"),
    skin=ramp("#0d0e1e", "#181c36", "#272e52", "#3f4a74", "#66769e", "#a4b4d2", "#dce6f4"),
    hair=ramp("#161c36", "#2b3a60", "#4d6690", "#859dc2", "#c4d4ea", "#ffffff"),
    bolt=ramp("#2f6bd8", "#6fd8ff", "#c8f8ff", "#ffffff"),
    gold=ramp("#3e260c", "#8c5f18", "#cf9a30", "#f6d878"),
    ground=ramp("#04050a", "#0a0c18", "#13172b", "#1e243c"),
)

# Palette-cycle groups (runtime): eyes + bolt core pulse; sky flash in phase 2.
CYCLES = {"eyes": ["bolt"], "flash": ["sky", "cloud"]}


def paint() -> Canvas:
    P = PAL
    S, Hh, G, Bo = P["skin"], P["hair"], P["gold"], P["bolt"]
    cv = Canvas(W, H)
    sky(cv, P["sky"], 0, 150)

    L = (0.8, -0.55, 0.3)  # the bolt, up and right: grazing, dramatic.
    clouds(cv, 11, P["cloud"][:4], 0, 128, thresh=0.5, scale=26, light=L)
    rain(cv, 4, P["cloud"][2], count=110, length=7, slant=3)
    lightning(cv, 16, 0, 104, math.pi * 0.6, np.random.default_rng(7), Bo[1], Bo[0])

    cx, hy = 108, 54

    def body(mask, bevel, bias=0.0, light=L, amb=0.1):
        cv.shade(mask, S, light=light, bevel=bevel, dither=0.4, bias=bias, ambient=amb)

    # Torso as one mass; anatomy is carved into it, not stacked onto it.
    torso = cv.poly([(cx - 78, 162), (cx - 70, 110), (cx - 48, 90), (cx - 16, 83),
                     (cx + 16, 83), (cx + 46, 88), (cx + 66, 100), (cx + 80, 162)])
    torso |= cv.ellipse(cx - 54, 108, 26, 22) | cv.ellipse(cx + 54, 104, 24, 20)
    body(torso, 18)
    pecs = cv.ellipse(cx + 20, 112, 21, 13) | cv.ellipse(cx - 20, 113, 21, 13)
    body(pecs & torso, 7, bias=0.05)
    # Pectoral underline, sternum, abdominal grid, serratus.
    carve(cv, [(cx - 38, 118), (cx - 22, 126), (cx - 3, 122)], S[0], torso, 2)
    carve(cv, [(cx + 3, 122), (cx + 22, 125), (cx + 40, 116)], S[1], torso, 2)
    carve(cv, [(cx, 98), (cx, 162)], S[0], torso, 1)
    for y in (136, 148):
        carve(cv, [(cx - 14, y), (cx - 2, y + 2)], S[1], torso)
        carve(cv, [(cx + 2, y + 2), (cx + 14, y)], S[2], torso)
    for k in range(3):
        carve(cv, [(cx + 42, 124 + k * 7), (cx + 50, 120 + k * 7)], S[2], torso)
    # Highlights where the bolt grazes: collarbone, near pec, near delt.
    carve(cv, [(cx + 6, 96), (cx + 34, 92), (cx + 60, 96)], S[5], torso)
    carve(cv, [(cx + 14, 102), (cx + 34, 104)], S[4], torso)

    # Lowered far arm in shadow, static crawling over it.
    farm = cv.poly([(cx - 64, 100), (cx - 84, 116), (cx - 100, 142), (cx - 106, 162),
                    (cx - 88, 162), (cx - 78, 134), (cx - 58, 116)])
    body(farm, 8, bias=-0.12, amb=0.05)

    # Raised near arm: delt, bicep, forearm, fist round the bolt.
    bx, by = cx + 112, 34
    AL = (0.6, -0.75, 0.3)
    upper = cv.poly([(cx + 50, 90), (cx + 72, 80), (cx + 92, 64), (cx + 104, 72),
                     (cx + 88, 98), (cx + 66, 112)])
    body(upper, 10, light=AL)
    bicep = cv.ellipse(cx + 82, 80, 14, 9)
    body(bicep, 6, bias=0.05, light=AL)
    carve(cv, [(cx + 70, 92), (cx + 86, 86), (cx + 96, 78)], S[1], upper | bicep)
    fore = cv.poly([(cx + 92, 62), (cx + 98, 50), (cx + 106, 38), (cx + 118, 40),
                    (cx + 110, 58), (cx + 104, 72)])
    body(fore, 6, light=(0.7, -0.6, 0.35))

    # Hair: storm-swept locks streaming left, back into the clouds.
    hair = np.zeros((H, W), bool)
    for k in range(14):
        r = np.random.default_rng(400 + k)
        sx = cx + 20 - k * 3.2
        sy = hy - 24 + abs(k - 6) * 1.3
        length = 40 + r.uniform(0, 22)
        m = streak(cv, sx - length, sy + 12 + k * 1.8, length, -(12 + k * 1.8),
                   -(6 + k % 3 * 3), 3.4)
        cv.shade(m, Hh, light=L, bevel=2.5, dither=0.3, bias=-0.15 + (k % 2) * 0.12,
                 ambient=0.15)
        hair |= m
    # Head: heavy brow, burning eyes, a stern nose.
    head = cv.ellipse(cx, hy, 20, 25)
    body(head, 12, bias=0.03)
    body(cv.ellipse(cx + 12, hy + 6, 7, 6), 3, bias=0.05)
    brow = cv.poly([(cx - 19, hy - 7), (cx - 3, hy - 10), (cx, hy - 6), (cx + 3, hy - 10),
                    (cx + 19, hy - 7), (cx + 18, hy - 3), (cx - 18, hy - 3)])
    body(brow, 2, bias=0.15)
    cv.fill(cv.ellipse(cx - 9, hy, 6, 3.4) | cv.ellipse(cx + 9, hy, 6, 3.4), S[0])
    for ex in (cx - 9, cx + 9):
        cv.fill(cv.ellipse(ex, hy, 5.5, 2.8), Bo[0])
        cv.fill(cv.rect(ex - 4, hy - 1, ex + 3, hy), Bo[1])
        cv.fill(cv.rect(ex - 2, hy - 1, ex + 1, hy), Bo[3])
        cv.put(ex + 4, hy - 2, Bo[1])
        cv.put(ex + 5, hy - 3, Bo[0])
    nose = cv.poly([(cx, hy - 5), (cx + 3, hy - 5), (cx + 6, hy + 10), (cx - 3, hy + 11)])
    body(nose, 2, bias=0.2)
    cv.fill(cv.rect(cx - 3, hy + 11, cx + 5, hy + 11), S[0])
    carve(cv, [(cx - 14, hy - 12), (cx - 4, hy - 13)], S[2], head)
    carve(cv, [(cx + 4, hy - 13), (cx + 14, hy - 12)], S[4], head)

    cap = cv.ellipse(cx + 1, hy - 17, 22, 12) & ~cv.ellipse(cx, hy - 2, 19, 12)
    cv.shade(cap, Hh, light=L, bevel=3, dither=0.3, ambient=0.15)
    for k in range(7):
        carve(cv, [(cx - 18 + k * 6, hy - 9), (cx - 22 + k * 6, hy - 20), (cx - 30 + k * 5, hy - 26)],
              Hh[1], cap)

    # Laurel crown, the one warm note on him.
    for i in range(-6, 7):
        t = i / 6
        cv.shade(cv.ellipse(cx + t * 21, hy - 13 - (1 - t * t) * 5, 3.2, 1.8), G,
                 light=L, bevel=1.5, dither=0.3, ambient=0.2)
    cv.fill(cv.ellipse(cx, hy - 19, 2.5, 2.5), G[3])

    # Beard: one shaded mass, strands carved, tips splitting into storm.
    beard = cv.poly([(cx - 21, hy + 13), (cx - 12, hy + 16), (cx + 12, hy + 16), (cx + 21, hy + 13), (cx + 30, hy + 36),
                     (cx + 26, hy + 64), (cx + 12, hy + 88), (cx, hy + 96),
                     (cx - 14, hy + 86), (cx - 28, hy + 62), (cx - 30, hy + 34)])
    for k in range(9):
        t = (k - 4) / 4
        beard |= lock(cv, cx + t * 22, hy + 40, 40 + (1 - abs(t)) * 24, 4, 5, 700 + k)
    cv.shade(beard, Hh, light=L, bevel=8, dither=0.35, ambient=0.15)
    for k in range(13):
        r = np.random.default_rng(800 + k)
        t = (k - 6) / 6
        x0 = cx + t * 24
        pts = [(x0 + math.sin(i * 0.7 + k) * 3 + t * i * 1.8, hy + 24 + i * 5.5)
               for i in range(int(8 + (1 - abs(t)) * 5))]
        carve(cv, pts, Hh[0] if k % 2 else Hh[1], beard)
        carve(cv, [(x + 2, y) for x, y in pts[1:4]], Hh[4], beard)
    must = cv.poly([(cx - 17, hy + 18), (cx - 1, hy + 12), (cx + 2, hy + 12), (cx + 18, hy + 18),
                    (cx + 24, hy + 30), (cx + 9, hy + 21), (cx, hy + 18), (cx - 9, hy + 21),
                    (cx - 23, hy + 30)])
    cv.shade(must, Hh, light=L, bevel=2, dither=0.3, ambient=0.25)

    # Front storm bank swallowing the waist.
    bank = (noise(W, H, 23, 14.0, 4) + (YY - 120) / 58.0) > 0.95
    bank &= YY < 162
    cv.shade(bank, P["cloud"], light=L, bevel=6, dither=0.5)

    # Halo of the bolt (sky-coloured, never the bolt's own steps).
    fig = torso | upper | bicep | fore | head | hair | beard | farm
    halo = cv.ellipse(bx, by, 40, 40) & ~fig & ~bank
    cv.tone(halo, [P["sky"][2], P["sky"][3], P["sky"][4], P["cloud"][4]],
            np.clip(1 - np.hypot(XX - bx, YY - by) / 40, 0, 1) ** 1.3, 1.0)

    # The keraunos: double-ended thunderbolt, dark-keyed so it pops.
    zig = [(bx - 44, by + 40), (bx - 20, by + 16), (bx - 26, by + 10), (bx + 2, by - 4),
           (bx - 4, by - 10), (bx + 26, by - 28), (bx + 20, by - 32), (bx + 48, by - 50)]
    bolt = taper(cv, zig, 7)
    cv.fill(dilate(bolt, True) & ~bolt, P["sky"][0])
    cv.fill(bolt, Bo[1])
    cv.fill(taper(cv, zig, 4.5), Bo[2])
    cv.fill(taper(cv, zig, 2.2), Bo[3])
    fist = cv.ellipse(bx, by + 1, 8, 8)
    cv.fill(dilate(fist) & ~fist & ~bolt, S[0])
    body(fist, 4, bias=0.2, light=(0.3, -0.9, 0.4))
    for gx in (bx - 5, bx - 1, bx + 3):
        cv.fill(cv.rect(gx, by - 5, gx + 1, by + 7) & fist, S[2])
    rng = np.random.default_rng(42)
    for a in (-0.9, 2.5):
        lightning(cv, bx, by, 26, a - math.pi * 0.25, rng, Bo[2], Bo[0], branch=0.25)

    # Rim light from the bolt on every edge that faces it.
    rim_light(cv, fig | fist, Bo[1], (1, -1), clip=(YY < 162) & ~bank & ~bolt)

    # Ground: a black plain, a strike, and the army for scale.
    cv.gradient(YY >= 152, P["ground"][::-1], 152, 192, dither=1.0)
    cv.fill(cv.poly([(0, 157), (40, 150), (90, 155), (150, 151), (210, 156), (256, 149),
                     (256, 161), (0, 163)]), P["ground"][1])
    lightning(cv, 218, 0, 150, math.pi * 0.52, np.random.default_rng(3), Bo[3], Bo[1],
              branch=0.25)
    cv.fill(cv.ellipse(222, 164, 20, 3.5), Bo[0])
    cv.fill(cv.ellipse(222, 164, 9, 1.6), Bo[2])
    soldiers_row(cv, 177, 10, 150, 5, (P["ground"][0], P["cloud"][2]), 1)
    soldiers_row(cv, 188, 2, 176, 6, (P["ground"][0], P["cloud"][1]), 2, height=9)
    return cv

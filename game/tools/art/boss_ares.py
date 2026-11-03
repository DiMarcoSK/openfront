"""boss_ares.py - ARES, the Bronze Colossus.

War itself, standing. A full-height giant in bronze plate astride a burning
field, seen from the ground so the legs tower and the helm is small and far.
The army marches between his feet. The Corinthian helm hides the face; only
the furnace behind the eye-slits shows. The crest is fire; the shield bears
the apotropaic eye Greek hoplites painted to glare down the enemy. Fire and
eye-glow are palette-cycled.
"""

import math

import numpy as np

from px import Canvas, ramp, noise, dilate
from stage import W, H, YY, XX, sky, soldiers_row, rim_light, taper, carve, bezier, tube

PAL = dict(
    sky=ramp("#120204", "#2a0607", "#48100c", "#6e1c10", "#9a3216"),
    smoke=ramp("#080505", "#140d0c", "#231816", "#36251f", "#4d362a"),
    bronze=ramp("#1a0a04", "#3c1a08", "#68320e", "#9a5418", "#cc8430", "#f0bc5c", "#fff0b4"),
    cloak=ramp("#1a0306", "#3e070c", "#6c1014", "#a01e1c"),
    fire=ramp("#6a1406", "#c23c0c", "#f47c1c", "#ffc04a", "#fff2a8"),
    leather=ramp("#140a06", "#2c180e", "#4a2c18", "#6e4628"),
    flesh=ramp("#1e0e0a", "#3e2018", "#64382a", "#8e5640", "#b87a5c"),
    bone=ramp("#4a3a2a", "#8a7454", "#cdb88c", "#f2e6c4"),
    ground=ramp("#040202", "#0e0606", "#1c0d0a", "#2e1610"),
)

CYCLES = {"fire": ["fire"]}


def paint() -> Canvas:
    P = PAL
    Bz, Ck, Fi, Le, Sm, Fl = P["bronze"], P["cloak"], P["fire"], P["leather"], P["smoke"], P["flesh"]
    cv = Canvas(W, H)
    sky(cv, P["sky"], 0, 150)
    L = (-0.6, -0.3, 0.35)  # low warm key from the burning plain, front-left.

    # Smoke columns rolling up from the burning plain.
    sm = noise(W, H, 81, 16.0, 4)
    col = np.clip(np.abs(np.sin(XX / 41.0 + 0.6)) * 1.1 - 0.15, 0, 1)
    smoke = (sm * 0.7 + col * 0.45 + (YY - 40) / 300) > 0.72
    cv.shade(smoke, Sm, light=(0.0, 0.8, 0.4), bevel=5, dither=0.6, ambient=0.15)

    cx = 128

    def plate(mask, bevel, bias=-0.12, amb=0.04, light=L):
        cv.shade(mask, Bz, light=light, bevel=bevel, dither=0.4, ambient=amb, bias=bias)

    def skin(mask, bevel):
        cv.shade(mask, Fl, light=L, bevel=bevel, dither=0.4, ambient=0.1)

    # Cloak behind: a crimson banner snapping to the right.
    cloak = cv.poly([(cx - 30, 40), (cx + 28, 38), (cx + 92, 52), (cx + 122, 80), (cx + 106, 96),
                     (cx + 124, 116), (cx + 80, 122), (cx + 40, 100), (cx - 10, 108)])
    cv.shade(cloak, Ck, light=L, bevel=6, dither=0.4, ambient=0.15)
    for k in range(4):
        carve(cv, bezier((cx + 10 + k * 14, 44), (cx + 40 + k * 14, 58), (cx + 60 + k * 12, 80),
                         (cx + 70 + k * 12, 114), 16), Ck[0], cloak)

    # Legs astride: long, the camera at ground level.
    legs = np.zeros((H, W), bool)
    for side, foot in ((-1, (cx - 46, 170)), (1, (cx + 46, 171))):
        thigh = cv.poly([(cx + side * 2, 92), (cx + side * 32, 88), (foot[0] + side * 11, 126),
                         (foot[0] - side * 10, 132)])
        thigh |= cv.ellipse((cx + side * 18 + foot[0]) / 2, 110, 11, 15)
        skin(thigh, 5)
        shin = cv.poly([(foot[0] - 9, 128), (foot[0] + 9, 128), (foot[0] + 7, foot[1] - 4),
                        (foot[0] - 7, foot[1] - 4)])
        plate(shin, 4)
        carve(cv, [(foot[0] - 1, 133), (foot[0] - 1, foot[1] - 6)], Bz[4], shin)
        knee = cv.ellipse(foot[0], 130, 9, 6)
        plate(knee, 3, bias=-0.05)
        ft = cv.poly([(foot[0] - 8, foot[1] - 5), (foot[0] + 8, foot[1] - 5),
                      (foot[0] + 8 + side * 8, foot[1]), (foot[0] - 10, foot[1])])
        cv.shade(ft, Le, light=L, bevel=2, ambient=0.2)
        legs |= thigh | shin | knee | ft

    # Pteruges: the leather-strip skirt under the cuirass.
    for k in range(9):
        x = cx - 30 + k * 7
        st = cv.rect(x, 84, x + 5, 100 + (k % 2) * 3)
        cv.shade(st, Le, light=L, bevel=2, ambient=0.2)
        cv.fill(cv.rect(x, 98 + (k % 2) * 3, x + 5, 100 + (k % 2) * 3), Bz[3])

    # Muscled cuirass.
    torso = cv.poly([(cx - 30, 40), (cx + 30, 40), (cx + 27, 66), (cx + 23, 86), (cx - 23, 86),
                     (cx - 27, 66)])
    plate(torso, 14)
    for side in (-1, 1):
        plate(cv.ellipse(cx + side * 13, 52, 13, 8) & torso, 5, bias=-0.06)
    carve(cv, [(cx - 24, 59), (cx - 12, 62), (cx, 59), (cx + 12, 62), (cx + 24, 59)], Bz[1], torso, 2)
    carve(cv, [(cx, 44), (cx, 84)], Bz[1], torso)
    for y in (68, 76):
        carve(cv, [(cx - 14, y), (cx - 2, y + 2)], Bz[2], torso)
        carve(cv, [(cx + 2, y + 2), (cx + 14, y)], Bz[3], torso)
    belt = cv.rect(cx - 25, 82, cx + 25, 86)
    plate(belt, 2)
    for x in range(cx - 21, cx + 23, 7):
        cv.fill(cv.ellipse(x, 84, 1.4, 1.4), Bz[6])
    for side in (-1, 1):
        plate(cv.ellipse(cx + side * 33, 44, 13, 10), 6)

    # Spear arm (his left, our right) planting the dory beside him.
    sx = cx + 66
    arm = tube(cv, [(cx + 36, 46), (cx + 52, 62), (sx - 2, 76)], 7, 5)
    skin(arm, 4)
    bracer = tube(cv, [(cx + 50, 64), (sx - 2, 76)], 5.5, 5)
    plate(bracer, 3)
    shaft = cv.rect(sx - 1, 4, sx + 1, 168)
    cv.fill(dilate(shaft) & ~shaft & ~arm & ~bracer, Le[0])
    cv.shade(shaft, Le, light=L, bevel=1, ambient=0.3)
    blade = cv.poly([(sx, -6), (sx + 6, 10), (sx + 3, 22), (sx, 26), (sx - 3, 22), (sx - 6, 10)])
    cv.fill(dilate(blade, True) & ~blade, Le[0])
    plate(blade, 3, bias=0.1)
    carve(cv, [(sx, -2), (sx, 24)], Bz[6], blade)
    fist = cv.ellipse(sx, 77, 5, 5)
    skin(fist, 3)
    carve(cv, [(sx - 4, 75), (sx + 4, 75)], Fl[0], fist)

    # Helm: Corinthian, cheek-guards closed, furnace behind the slits.
    hx, hy = cx, 22
    crest = taper(cv, [(hx - 26, 18), (hx - 12, 2), (hx + 8, -2), (hx + 28, 4), (hx + 40, 16)],
                  8, ends=(3, 2))
    fn = noise(W, H, 91, 4.0, 2)
    cv.tone(crest, Fi, np.clip((18 - YY) / 20.0 + fn * 0.5, 0, 1), 1.0)
    tongues = np.zeros_like(crest)
    for k in range(8):
        x = hx - 22 + k * 8
        tongues |= taper(cv, [(x, 4 - math.sin(k) * 3), (x + 9, -8 - (k % 3) * 4)], 2.2, ends=(2, 0))
    cv.fill(tongues & ~crest, Fi[2])
    cv.fill(dilate(tongues & ~crest) & ~tongues & ~crest & (YY < 18), Fi[1])
    helm = cv.ellipse(hx, hy, 13, 15) | cv.poly([(hx - 12, hy), (hx + 12, hy), (hx + 9, hy + 18),
                                                 (hx + 3, hy + 20), (hx - 3, hy + 20), (hx - 9, hy + 18)])
    cv.fill(dilate(helm, True) & ~helm, Bz[0])
    plate(helm, 7, bias=-0.02)
    plate(cv.rect(hx - 2, hy - 17, hx + 2, hy - 12), 2)
    slits = cv.poly([(hx - 10, hy - 1), (hx - 2, hy + 1), (hx - 2, hy + 4), (hx - 9, hy + 3)])
    slits |= cv.poly([(hx + 10, hy - 1), (hx + 2, hy + 1), (hx + 2, hy + 4), (hx + 9, hy + 3)])
    cv.fill(slits, Fi[2])
    cv.fill(slits & np.roll(slits, 1, axis=0) & np.roll(slits, -1, axis=0), Fi[4])
    cv.fill(cv.rect(hx - 1, hy + 1, hx, hy + 11), Bz[3])  # nose guard
    cv.fill(cv.rect(hx - 1, hy + 13, hx, hy + 19), Fi[0])  # mouth gap
    carve(cv, [(hx - 11, hy - 5), (hx, hy - 3), (hx + 11, hy - 5)], Bz[5], helm)
    carve(cv, [(hx - 9, hy + 5), (hx - 7, hy + 17)], Bz[1], helm)
    carve(cv, [(hx + 9, hy + 5), (hx + 7, hy + 17)], Bz[4], helm)

    # The aspis on his near arm: bronze rim, crimson field, the war-eye.
    sx0, sy0, r = cx - 50, 72, 32
    shield = cv.ellipse(sx0, sy0, r, r)
    cv.fill(dilate(shield, True) & ~shield, Bz[0])
    plate(shield, 6, bias=-0.04)
    field = cv.ellipse(sx0, sy0, r - 6, r - 6)
    cv.shade(field, Ck, light=L, bevel=10, dither=0.4, ambient=0.25)
    for a in range(0, 360, 30):
        cv.fill(cv.ellipse(sx0 + math.cos(math.radians(a)) * (r - 3),
                           sy0 + math.sin(math.radians(a)) * (r - 3), 1.5, 1.5), Bz[5])
    lid = cv.poly([(sx0 - 22, sy0), (sx0 - 10, sy0 - 9), (sx0 + 4, sy0 - 11), (sx0 + 22, sy0 - 2),
                   (sx0 + 8, sy0 + 8), (sx0 - 8, sy0 + 8)])
    cv.fill(dilate(lid, True) & ~lid, Bz[0])
    cv.shade(lid, P["bone"], light=L, bevel=3, ambient=0.3)
    cv.fill(cv.ellipse(sx0 + 1, sy0 - 1, 7.5, 7.5) & lid, Bz[0])
    cv.fill(cv.ellipse(sx0 + 1, sy0 - 1, 6, 6) & lid, Fi[1])
    cv.fill(cv.ellipse(sx0 + 1, sy0 - 1, 3, 4.5) & lid, Bz[0])
    cv.put(sx0 - 1, sy0 - 4, Fi[4])
    for k in range(7):
        a = math.pi * (1.1 + k * 0.13)
        carve(cv, [(sx0 + math.cos(a) * 13, sy0 - 4 + math.sin(a) * 11),
                   (sx0 + math.cos(a) * 21, sy0 - 4 + math.sin(a) * 18)], Bz[1], field)
    carve(cv, [(sx0 - 20, sy0 + 4), (sx0 - 6, sy0 + 12), (sx0 + 10, sy0 + 12), (sx0 + 22, sy0 + 2)],
          Bz[0], field, 2)

    # Backlight: the burning plain rims every lower/right edge in fire.
    fig = torso | helm | shield | cloak | arm | legs
    rim_light(cv, fig, Fi[2], (1, 1), clip=YY < 166)
    rim_light(cv, fig, Bz[5], (-1, -1), clip=(YY < 150) & ~cloak & ~shield)

    # The burning plain.
    ground = (YY >= 150) & ~legs
    cv.gradient(ground, P["ground"][::-1], 150, 192, 1.0)
    flames = np.zeros((H, W), bool)
    for k in range(34):
        r = np.random.default_rng(1500 + k)
        x = r.uniform(0, W)
        h = r.uniform(6, 16)
        flames |= taper(cv, [(x, 156 + r.uniform(-2, 4)), (x + r.uniform(-3, 3), 156 - h)],
                        2.4 + h / 8, ends=(3, 0))
    flames &= ~(legs & (YY > 150))
    cv.tone(flames, Fi, np.clip((158 - YY) / 16.0 + noise(W, H, 95, 3.0, 2) * 0.4, 0, 1), 1.0)
    cv.fill(cv.poly([(0, 158), (256, 156), (256, 162), (0, 164)]) & ~flames & ~legs, P["ground"][2])
    for k in range(60):
        r = np.random.default_rng(1600 + k)
        cv.put(r.uniform(0, W), r.uniform(0, 150), Fi[3 if k % 3 else 4])
    soldiers_row(cv, 180, 66, 190, 5, (P["ground"][0], Fi[0]), 11)
    soldiers_row(cv, 190, 40, 220, 6, (P["ground"][0], Fi[1]), 12, height=9)
    return cv

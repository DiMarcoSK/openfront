"""boss_poseidon.py - POSEIDON, the Abyssal Leviathan.

Not a bearded man with a fork: the sea-god arrives as the leviathan he
rides, a neck the width of a fleet rising from a breaking wave, wearing a
trident of bronze-coral as its crown. A split trireme sinks in its coils for
scale. Bioluminescent eyes and the wave's foam lip are palette-cycled.
"""

import math

import numpy as np

from px import Canvas, ramp, noise, dilate
from stage import W, H, YY, XX, sky, clouds, soldiers_row, rim_light, taper, carve, \
    bezier, tube, scales, foam

PAL = dict(
    sky=ramp("#06121a", "#0b1f2a", "#12313b", "#1c4549", "#2c5e58"),
    cloud=ramp("#0a1820", "#14303a", "#22474e", "#356462", "#5a8a80"),
    sea=ramp("#020a10", "#06171f", "#0b2830", "#123c42", "#1e5856", "#33796e"),
    foam=ramp("#5aa298", "#a6d8cc", "#e8f8f2"),
    hide=ramp("#030d12", "#08202a", "#0f343c", "#185050", "#246c64", "#3a8f7c", "#6cbca0"),
    belly=ramp("#2a3324", "#4e5a3c", "#7e8a5c", "#b4bc8a", "#e2e4bc"),
    crown=ramp("#3a1408", "#7a2c14", "#bc5428", "#ec8a4c", "#ffd0a0"),
    glow=ramp("#2ad8c0", "#9affea", "#ffffff"),
    wood=ramp("#160c08", "#2e1a10", "#4e3020", "#74503a"),
)

CYCLES = {"eyes": ["glow"], "surf": ["foam"]}


def paint() -> Canvas:
    P = PAL
    Hd, Be, Cr, Gl = P["hide"], P["belly"], P["crown"], P["glow"]
    cv = Canvas(W, H)
    sky(cv, P["sky"], 0, 110)
    L = (-0.6, -0.75, 0.35)  # the moon, up and left.
    cv.fill(cv.ellipse(38, 30, 13, 13), P["foam"][1])
    cv.fill(cv.ellipse(42, 27, 11, 11), P["sky"][2])
    clouds(cv, 31, P["cloud"], 0, 80, thresh=0.53, scale=24, light=L)

    # The breaking wave: a wall of water curling over from the right.
    wave = cv.poly([(60, 140), (110, 92), (160, 58), (206, 34), (240, 26), (256, 26),
                    (256, 150), (60, 150)])
    lip = cv.poly([(150, 64), (196, 36), (232, 22), (256, 18), (256, 34), (236, 38),
                   (214, 48), (200, 62), (204, 52), (180, 58)])
    wn = noise(W, H, 9, 10.0, 3)
    v = np.clip((XX - 60) / 200 * 0.5 + wn * 0.5 - (YY - 30) / 500, 0, 1)
    cv.tone(wave, P["sea"][2:] + [P["foam"][0]], v, 1.0)
    cv.shade(lip, P["foam"], light=L, bevel=2, dither=0.5, ambient=0.3)
    foam(cv, 12, P["foam"][0], wave & (YY < 70) & dilate(dilate(lip)), 0.5)
    spray = (noise(W, H, 13, 3.0, 2) > 0.62) & cv.ellipse(214, 22, 46, 14) & ~lip
    cv.fill(spray, P["foam"][1])

    # Far coil breaking the surface at left: the body goes on for miles.
    coil = tube(cv, bezier((8, 150), (14, 112), (52, 106), (64, 150)), 9, 9)
    cv.shade(coil, Hd, light=L, bevel=6, dither=0.4, ambient=0.08, bias=-0.1)
    for k in range(6):
        sp = taper(cv, [(18 + k * 7, 118 - math.sin(k / 5 * math.pi) * 5),
                        (14 + k * 7, 104 - math.sin(k / 5 * math.pi) * 6)], 2.4)
        cv.shade(sp, Cr[:3], light=L, bevel=1, ambient=0.2)

    # The neck: rises from the sea at right, arcs over, head lunges down-left.
    spine = bezier((206, 168), (238, 92), (196, 22), (128, 30), 40)
    neck = tube(cv, spine, 26, 16)
    under = tube(cv, spine, 26, 16, side=-1) & ~tube(cv, spine, 14, 8, side=-1)
    dorsal = np.zeros_like(neck)
    for i in range(4, 38, 4):
        x, y = spine[i]
        a = spine[i + 1]
        dx, dy = a[0] - x, a[1] - y
        ln = math.hypot(dx, dy)
        tx, ty = dx / ln, dy / ln
        nx, ny = -ty, tx
        w = 26 + (16 - 26) * i / 40 - 2
        bx, by = x + nx * w, y + ny * w
        ln_ = 15 - i * 0.2
        dorsal |= cv.poly([(bx - tx * 5, by - ty * 5), (bx + tx * 5, by + ty * 5),
                           (bx + nx * ln_ - tx * 7, by + ny * ln_ - ty * 7)])
    cv.fill(dilate(dorsal, True) & ~dorsal, Hd[0])
    cv.shade(dorsal, Cr[:4], light=L, bevel=2, dither=0.3, ambient=0.15)
    cv.shade(neck, Hd, light=L, bevel=13, dither=0.4, ambient=0.1)
    scales(cv, neck & ~under, Hd[1], 5)
    cv.shade(under & neck, Be, light=L, bevel=4, dither=0.4, ambient=0.15, bias=-0.1)
    for i in range(0, 40, 2):
        x, y = spine[i]
        a = spine[min(39, i + 1)]
        dx, dy = a[0] - x, a[1] - y
        ln = math.hypot(dx, dy) or 1
        nx, ny = dy / ln, -dx / ln
        w = 26 + (16 - 26) * i / 40
        carve(cv, [(x + nx * 2, y + ny * 2), (x + nx * w, y + ny * w)], Be[0], under & neck)

    # Head: long snout lunging down-left, jaws agape.
    hx, hy = 118, 46
    skull = cv.poly([(hx + 22, hy - 20), (hx + 6, hy - 22), (hx - 18, hy - 12), (hx - 46, hy + 6),
                     (hx - 58, hy + 18), (hx - 54, hy + 22), (hx - 28, hy + 14), (hx - 2, hy + 10),
                     (hx + 22, hy + 10)])
    jaw = cv.poly([(hx + 12, hy + 8), (hx - 6, hy + 18), (hx - 30, hy + 34), (hx - 46, hy + 46),
                   (hx - 42, hy + 50), (hx - 18, hy + 42), (hx + 8, hy + 30), (hx + 22, hy + 16)])
    maw = cv.poly([(hx + 14, hy + 10), (hx - 28, hy + 14), (hx - 52, hy + 22), (hx - 42, hy + 46),
                   (hx - 16, hy + 36), (hx + 8, hy + 22)])
    cv.fill(maw, Hd[0])
    cv.fill(cv.ellipse(hx - 6, hy + 20, 14, 6) & maw, P["crown"][0])
    for k in range(9):  # upper fangs
        t = k / 8
        x = hx + 10 - t * 60
        y = hy + 10 + t * 10
        cv.fill(taper(cv, [(x, y), (x - 1, y + 4 + (k % 2) * 3)], 1.4, ends=(1.6, 0)), Be[4])
    for k in range(7):  # lower fangs
        t = k / 6
        x = hx + 2 - t * 44
        y = hy + 26 + t * 18
        cv.fill(taper(cv, [(x, y), (x + 1, y - 4 - (k % 2) * 2)], 1.3, ends=(1.5, 0)), Be[3])
    cv.shade(jaw, Hd, light=L, bevel=5, dither=0.4, ambient=0.08, bias=-0.05)
    cv.shade(cv.poly([(hx + 12, hy + 26), (hx - 30, hy + 40), (hx - 44, hy + 49), (hx - 40, hy + 46),
                      (hx + 10, hy + 22)]) & jaw, Be, light=L, bevel=2, ambient=0.2)
    cv.shade(skull, Hd, light=L, bevel=8, dither=0.4, ambient=0.12)
    scales(cv, skull & cv.rect(hx - 20, 0, W, H), Hd[2], 4)
    # Brow ridge and three burning eyes per side (we see one side).
    ridge = taper(cv, [(hx + 16, hy - 16), (hx - 4, hy - 12), (hx - 30, hy - 2)], 3.4)
    cv.shade(ridge, Hd[2:], light=L, bevel=2, ambient=0.3)
    for k, (ex, ey) in enumerate(((hx - 20, hy - 3), (hx - 8, hy - 7), (hx + 3, hy - 9))):
        r = 3.2 - k * 0.6
        cv.fill(cv.ellipse(ex, ey, r + 1.6, r + 0.4), Hd[0])
        cv.fill(cv.ellipse(ex, ey, r + 0.6, r - 0.4), Gl[0])
        cv.fill(cv.ellipse(ex, ey, max(r - 1.0, 0.8), max(r - 1.6, 0.6)), Gl[2])
    for k in range(4):  # nostril and lip grooves
        carve(cv, [(hx - 50 + k * 9, hy + 13 - k * 2), (hx - 44 + k * 9, hy + 12 - k * 2)], Hd[0], skull)

    # The trident crown: three bronze-coral tines sweeping back off the skull.
    tines = [((hx + 2, hy - 18), (hx + 2, hy - 46), 3.6),
             ((hx - 8, hy - 16), (hx - 14, hy - 38), 3.0),
             ((hx + 12, hy - 18), (hx + 20, hy - 38), 3.0)]
    for (x0, y0), (x1, y1), w in tines:
        t = taper(cv, [(x0, y0), (x1, y1)], w, ends=(w, 0))
        t |= taper(cv, [(x1 + (x0 - x1) * 0.3, y1 + (y0 - y1) * 0.3),
                        (x1 + (x0 - x1) * 0.3 + (2 if x1 >= x0 else -2) + (x1 - x0) * 0.2,
                         y1 + (y0 - y1) * 0.3 - 7)], 1.5, ends=(1.5, 0))
        cv.fill(dilate(t, True) & ~t & ~skull, Hd[0])
        cv.shade(t, Cr, light=L, bevel=2, dither=0.3, ambient=0.2)
    band = taper(cv, [(hx - 10, hy - 14), (hx + 6, hy - 20), (hx + 22, hy - 18)], 3, ends=(2.5, 2.5))
    cv.shade(band, Cr, light=L, bevel=1.5, ambient=0.25)
    cv.fill(cv.ellipse(hx + 6, hy - 18, 2.4, 2.4), Gl[1])
    # Gill frills behind the jaw.
    for k in range(4):
        fr = taper(cv, [(hx + 18, hy + 4 + k * 5), (hx + 34 + k * 2, hy + 2 + k * 8)], 2.6)
        cv.shade(fr & ~neck, Cr[:4], light=L, bevel=1, ambient=0.15)

    # Moonlit rim on everything that faces up-left.
    beast = neck | skull | jaw | coil
    cv.fill(dilate(beast, True) & ~beast & ~dorsal & (YY < 140) & ~cv.ellipse(hx + 4, hy - 30, 30, 20), Hd[0])
    rim_light(cv, beast, Hd[6], (-1, -1))
    rim_light(cv, wave & ~lip, P["sea"][5], (-1, -1))

    # Water line, the sinking trireme, the beach, the army.
    sea = (YY >= 140) & (YY < 166)
    v = np.clip((YY - 140) / 26 + noise(W, H, 44, 6.0, 2) * 0.4 - 0.2, 0, 1)
    cv.tone(sea, P["sea"][1:5][::-1], v, 1.0)
    for k in range(14):
        y = 143 + (k * 7) % 22
        x = (k * 37) % 240
        cv.fill(cv.rect(x, y, x + 8 + k % 4 * 3, y), P["foam"][0])
    churn = (noise(W, H, 51, 4.0, 2) > 0.5) & cv.ellipse(206, 160, 34, 7)
    cv.fill(churn, P["foam"][2])
    cv.fill((noise(W, H, 52, 3.0, 2) > 0.55) & cv.ellipse(206, 160, 44, 9) & ~churn,
            P["foam"][1])
    # Trireme snapped in two: bow and stern rearing, oars like straw.
    W_ = P["wood"]
    halves = ([(64, 166), (96, 160), (110, 142), (116, 145), (106, 168), (68, 170)],
              [(128, 170), (132, 150), (140, 146), (156, 160), (160, 170)])
    for poly in halves:
        hull = cv.poly(poly)
        cv.fill(dilate(hull, True) & ~hull, W_[0])
        cv.shade(hull, W_, light=L, bevel=2, ambient=0.25)
        for k in range(3):
            carve(cv, [(poly[0][0], poly[0][1] - 2 - k * 3), (poly[2][0], poly[2][1] + 4 - k * 2)],
                  W_[1], hull)
    carve(cv, [(68, 164), (96, 158), (110, 142)], W_[3], None, 1)
    carve(cv, [(130, 156), (140, 147), (156, 159)], W_[3], None, 1)
    for k, x in enumerate((74, 80, 86, 92, 136, 142, 148)):
        y = 162 - (x - 70) // 8 if x < 120 else 154 + (x - 140) // 6
        cv.fill(cv.ellipse(x, y, 2.2, 2.2), P["crown"][2] if k % 2 else P["belly"][3])
        cv.put(x, y, W_[0])
    cv.fill(cv.ellipse(108, 147, 3, 2), P["crown"][2])  # bow eye
    carve(cv, [(98, 158), (90, 122)], W_[0], None, 2)
    carve(cv, [(90, 122), (86, 116)], W_[1], None, 1)
    sail = cv.poly([(91, 126), (76, 132), (80, 146), (84, 140), (88, 148), (94, 136)])
    cv.shade(sail, P["belly"][1:4], light=L, bevel=2, ambient=0.3)
    carve(cv, [(84, 128), (86, 145)], P["crown"][1], sail, 2)
    for k in range(8):
        carve(cv, [(72 + k * 4, 164), (64 + k * 5, 172)], W_[3])
        carve(cv, [(134 + k * 3, 162), (140 + k * 4, 172)], W_[2])
    shore = YY >= 166
    cv.gradient(shore, ramp("#1a1a14", "#11120e", "#08090a"), 166, 192, 1.0)
    cv.fill(cv.poly([(0, 168), (60, 165), (130, 169), (200, 166), (256, 168), (256, 172), (0, 172)]),
            P["sea"][3])
    soldiers_row(cv, 182, 8, 160, 5, (ramp("#050606")[0], P["cloud"][2]), 5)
    soldiers_row(cv, 191, 2, 190, 6, (ramp("#050606")[0], P["cloud"][1]), 6, height=9)
    return cv

"""boss_hades.py - HADES, the Gate of a Thousand Dead.

The underworld does not walk onto the field: it opens. A gate of black
pillars splits the horizon, and behind it the crowned skull-king leans
through, robes dissolving into the Styx, a river of souls that flows
toward the army. Soul-fire (eyes, crown, river) is palette-cycled.
"""

import math

import numpy as np

from px import Canvas, ramp, noise, dilate
from stage import W, H, YY, XX, sky, soldiers_row, rim_light, taper, carve, bezier, tube

PAL = dict(
    sky=ramp("#050308", "#0b0612", "#130a1e", "#1c0f2a", "#261436"),
    stone=ramp("#040306", "#0b0910", "#15121c", "#221d2c", "#342c40", "#4c4058"),
    robe=ramp("#030205", "#09060f", "#120c1c", "#1d1430", "#2c1f44"),
    bone=ramp("#2a2530", "#4c4452", "#7a7078", "#ada39c", "#ddd6c4", "#fbf6e6"),
    soul=ramp("#0f3a33", "#1d6e5a", "#3aae86", "#7ef0b8", "#d4ffe8"),
    ember=ramp("#3a0a0a", "#7a1a10", "#c4401c", "#f08a3a"),
    gold=ramp("#2a1a0a", "#5e3c14", "#a07028", "#e0b456"),
)

CYCLES = {"soulfire": ["soul"], "embers": ["ember"]}


def paint() -> Canvas:
    P = PAL
    St, Ro, Bn, So, Em, G = P["stone"], P["robe"], P["bone"], P["soul"], P["ember"], P["gold"]
    cv = Canvas(W, H)
    sky(cv, P["sky"][::-1], 0, 140)
    L = (0.0, 0.6, 0.5)  # lit from below: soul-light rising off the Styx.

    # A green aurora of departing souls across the black sky.
    au = noise(W, H, 61, 18.0, 3)
    band = np.exp(-((YY - 40 - np.sin(XX / 30.0) * 12) / 16.0) ** 2) * au
    cv.tone(band > 0.32, [P["sky"][3], So[0]], np.clip((band - 0.32) * 4, 0, 1), 1.0)

    # Far cavern teeth framing the horizon.
    for k in range(10):
        x = k * 28 + (k % 3) * 5
        st = cv.poly([(x - 14, 140), (x - 3, 60 + (k * 37) % 40), (x + 2, 58 + (k * 37) % 40),
                      (x + 14, 140)])
        cv.shade(st, St[1:4], light=L, bevel=4, ambient=0.1)

    cx, sy = 128, 58
    # Robed bulk: a hood mountain whose hem frays into souls.
    robe = cv.poly([(cx - 30, 30), (cx + 30, 30), (cx + 62, 70), (cx + 96, 150), (cx - 96, 150),
                    (cx - 62, 70)])
    hood = cv.ellipse(cx, sy - 4, 36, 40)
    cv.shade(robe | hood, Ro, light=L, bevel=18, dither=0.5, ambient=0.05)
    for k in range(7):
        t = (k - 3) / 3
        sw = math.sin(k * 1.7) * 8
        fold = bezier((cx + t * 22, 52), (cx + t * 30 + sw, 86), (cx + t * 58 - sw, 116),
                      (cx + t * 88, 150), 24)
        carve(cv, fold, Ro[0], robe, 2)
        carve(cv, [(x + 2, y) for x, y in fold[10:]], Ro[3], robe)
    hollow = cv.ellipse(cx, sy, 25, 30)
    cv.fill(hollow, Ro[0])

    # The skull: long, regal, cheekbones like blades.
    skull = cv.ellipse(cx, sy - 4, 17, 19) | cv.poly([(cx - 12, sy + 6), (cx + 12, sy + 6),
                                                       (cx + 9, sy + 24), (cx - 9, sy + 24)])
    cv.shade(skull, Bn, light=(0.0, 0.5, 0.7), bevel=6, dither=0.4, ambient=0.15)
    for side in (-1, 1):
        sock = cv.poly([(cx + side * 3, sy - 4), (cx + side * 14, sy - 7), (cx + side * 13, sy + 3),
                        (cx + side * 4, sy + 3)])
        cv.fill(sock, Bn[0])
        cv.fill(cv.ellipse(cx + side * 8.5, sy - 1, 3.4, 2.4), So[2])
        cv.fill(cv.ellipse(cx + side * 8.5, sy - 1, 1.6, 1.2), So[4])
        carve(cv, [(cx + side * 15, sy + 5), (cx + side * 9, sy + 11), (cx + side * 9, sy + 18)],
              Bn[1], skull)
    nose = cv.poly([(cx, sy + 3), (cx + 3, sy + 10), (cx - 3, sy + 10)])
    cv.fill(nose, Bn[0])
    for k in range(-3, 4):
        cv.fill(cv.rect(cx + k * 3 - 1, sy + 16, cx + k * 3, sy + 20), Bn[4])
        cv.put(cx + k * 3 + 1, sy + 17, Bn[0])
        cv.put(cx + k * 3 + 1, sy + 18, Bn[0])
    carve(cv, [(cx - 10, sy + 18), (cx + 10, sy + 18)], Bn[0], skull)
    cv.fill(cv.rect(cx - 8, sy + 21, cx + 8, sy + 22), Bn[1])

    # Crown of black iron spikes with soul-fire flames on each point.
    for k in range(-3, 4):
        x = cx + k * 7
        h = 22 - abs(k) * 3
        sp = cv.poly([(x - 4, sy - 16 + abs(k)), (x + 4, sy - 16 + abs(k)), (x, sy - 16 - h)])
        cv.fill(dilate(sp, True) & ~sp, St[0])
        cv.shade(sp, G, light=(0.0, 0.7, 0.5), bevel=2, ambient=0.2)
        fl = taper(cv, [(x, sy - 15 - h), (x + math.sin(k) * 2, sy - 24 - h)], 2.4, ends=(2.4, 0))
        cv.tone(fl, So[2:], np.clip((sy - 14 - h - YY) / 10.0, 0, 1) * 0 + 0.5, 1.0)
    band = cv.rect(cx - 24, sy - 18, cx + 24, sy - 13) & cv.ellipse(cx, sy - 8, 26, 16)
    cv.shade(band, G, light=(0.0, 0.7, 0.5), bevel=2, ambient=0.2)
    for k in (-14, 0, 14):
        cv.fill(cv.ellipse(cx + k, sy - 16, 2, 2), So[3])

    # The gate: two pillars of black stone, a broken lintel, rune bands.
    for side in (-1, 1):
        px0 = cx + side * 84
        pil = cv.rect(px0 - 11, 40, px0 + 11, 156)
        cv.shade(pil, St, light=L, bevel=5, dither=0.5, ambient=0.1)
        for y in range(52, 150, 16):
            carve(cv, [(px0 - 11, y), (px0 + 11, y)], St[0], pil)
            cv.put(px0 - 5 + (y // 16) % 3 * 4, y + 6, So[1])
            cv.put(px0 - 4 + (y // 16) % 3 * 4, y + 7, So[2])
        cap = cv.rect(px0 - 16, 34, px0 + 16, 42)
        cv.shade(cap, St, light=L, bevel=2, ambient=0.15)
    lint = cv.poly([(cx - 104, 24), (cx - 20, 22), (cx - 14, 30), (cx - 26, 34), (cx - 104, 34)])
    lint |= cv.poly([(cx + 104, 24), (cx + 30, 20), (cx + 22, 28), (cx + 30, 34), (cx + 104, 34)])
    cv.shade(lint, St, light=L, bevel=3, ambient=0.1)
    carve(cv, [(cx - 100, 29), (cx - 30, 28)], So[1], lint)
    carve(cv, [(cx + 30, 27), (cx + 100, 29)], So[1], lint)

    # Skeletal hands of the god curl round the gate pillars.
    hands = []
    for side in (-1, 1):
        hx = cx + side * 72
        wrist = taper(cv, [(hx - side * 44, 98), (hx - side * 20, 86)], 7, ends=(7, 5))
        cv.shade(wrist & ~hollow, Ro, light=L, bevel=3, ambient=0.15)
        for f in range(4):
            fy = 72 + f * 6
            bones = [(hx - side * 20, 84 + f * 2), (hx - side * 8, fy), (hx + side * 2, fy - 1),
                     (hx + side * 9, fy + 3)]
            for i in range(3):
                seg = taper(cv, bones[i:i + 2], 1.0, ends=(1.6, 1.4))
                cv.fill(dilate(seg) & ~seg, Bn[0])
                cv.shade(seg, Bn[1:], light=L, bevel=1, ambient=0.35)
                hands.append(seg)
            for x, y in bones[1:3]:
                cv.fill(cv.ellipse(x, y, 1.8, 1.8), Bn[4])
                cv.put(x, y + 1, Bn[1])
        hands.append(wrist)

    # Soul-light rising: rim every robe/bone edge that faces down.
    fig = robe | hood
    rim_light(cv, fig, So[1], (0, 1), clip=YY < 150)

    # The Styx: a river of souls running from the gate toward the army.
    river = cv.poly([(cx - 70, 140), (cx + 70, 140), (cx + 150, 172), (cx + 150, 192),
                     (cx - 150, 192), (cx - 150, 172)])
    cv.fill(YY >= 140, St[1])
    fl = noise(W, H, 71, 6.0, 3)
    flow = np.clip(fl * 1.1 + np.sin((XX - YY * 1.6) / 5.0) * 0.16 - 0.32, 0, 1)
    cv.tone(river, [Ro[1], Ro[3], So[0], So[1]], flow, 1.0)
    for k in range(18):
        r = np.random.default_rng(900 + k)
        x = r.uniform(cx - 90, cx + 90)
        y = r.uniform(146, 186)
        face = cv.ellipse(x, y, 2.2, 2.6) & river
        cv.fill(face, So[3])
        cv.put(int(x) - 1, int(y), So[0])
        cv.put(int(x) + 1, int(y), So[0])
        cv.fill(taper(cv, [(x, y + 1), (x - 3, y - 8)], 1.4) & river & ~face, So[2])
    cv.fill(dilate(river) & ~river & (YY >= 140), Em[1])
    # Embers drifting up out of the gate.
    for k in range(40):
        r = np.random.default_rng(1200 + k)
        cv.put(r.uniform(20, 236), r.uniform(20, 140), Em[2 + (k % 2)])
    # Banks: the army lines the near bank, tiny against the gate.
    bank_l = cv.poly([(0, 176), (40, 172), (70, 182), (0, 192)])
    bank_r = cv.poly([(256, 174), (214, 172), (190, 184), (256, 192)])
    cv.fill(bank_l | bank_r, St[0])
    soldiers_row(cv, 188, 2, 64, 5, (St[0], St[4]), 9)
    soldiers_row(cv, 188, 196, 256, 5, (St[0], St[4]), 10)
    return cv

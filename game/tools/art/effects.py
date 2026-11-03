"""effects.py - what each god's attack does to the field (64x64, 6 frames).

Effects are drawn in the god's own 16-colour palette, so they share its OAM
palette bank: a strike costs one 64x64 sprite and no palette RAM.
"""

from __future__ import annotations

import math

import numpy as np

from px import Canvas, noise, dilate
from stage import taper
import gods

E = 64
FRAMES = 6


def _bolt_path(rng, x, y0, y1):
    pts, y = [(x, y0)], y0
    while y < y1:
        y += rng.uniform(5, 9)
        x += rng.uniform(-5, 5)
        pts.append((x, min(y, y1)))
    return pts


def zeus_strike(f) -> Canvas:
    """Bolt from the sky -> blinding hit -> scorched ring."""
    P = gods.ZEUS
    cv = Canvas(E, E)
    rng = np.random.default_rng(3)
    path = _bolt_path(rng, 32, 0, 52)
    if f <= 2:
        w = (1, 3, 2)[f]
        if f >= 1:
            cv.fill(cv.stroke(path, w + 2), P["bolt"][0])
        cv.fill(cv.stroke(path, w), P["bolt"][1] if f == 0 else P["white"][2])
        for k in range(f + 1):
            br = _bolt_path(np.random.default_rng(10 + k), path[2 + k][0], path[2 + k][1], 40 + k * 4)
            cv.fill(cv.stroke(br, 1), P["bolt"][1])
    if f >= 1:
        r = (0, 8, 14, 18, 20, 21)[f]
        ring = cv.ellipse(32, 54, r, r * 0.4)
        inner = cv.ellipse(32, 54, max(r - (4 if f <= 3 else 1.5), 1), max(r * 0.4 - (2 if f <= 3 else 1), 1))
        if f <= 3:
            cv.fill(ring, P["bolt"][0] if f > 2 else P["white"][1])
            cv.fill(inner, P["white"][2])
        else:
            cv.fill(ring & ~inner & ((np.mgrid[0:E, 0:E][1] + f) % 3 != 0), P["bolt"][f - 4])
            cv.fill(cv.ellipse(32, 54, 6, 2), P["cloud"][1])
    if f >= 3:
        for k in range(8):
            a = k / 8 * 2 * math.pi
            d = 6 + f * 4
            cv.put(32 + math.cos(a) * d, 54 + math.sin(a) * d * 0.4 - (f - 3) * 2, P["bolt"][1])
    return cv


def poseidon_wave(f) -> Canvas:
    """A wave rises, curls, crashes across the men, sprays out."""
    P = gods.POSEIDON
    cv = Canvas(E, E)
    h = (10, 24, 34, 26, 14, 6)[f]
    crest_x = (40, 34, 26, 18, 12, 8)[f]
    body = cv.poly([(4, 60), (60, 60), (60, 60 - h * 0.6), (crest_x + 14, 60 - h), (crest_x, 60 - h + 6),
                    (crest_x - 6, 60 - h * 0.4), (4, 56)])
    cv.shade(body, P["hide"][1:], light=(-0.6, -0.7, 0.45), bevel=4, ambient=0.2, dither=0.4)
    lip = dilate(body) & ~body & (np.mgrid[0:E, 0:E][0] < 60 - h * 0.5)
    cv.fill(lip, P["glow"][1])
    fo = (noise(E, E, 4 + f, 2.5, 2) > 0.6) & cv.ellipse(crest_x + 4, 60 - h, 10, 6)
    cv.fill(fo, P["glow"][1])
    if f >= 3:
        sp = (noise(E, E, 9 + f, 2.0, 2) > 0.62) & cv.ellipse(20, 56, 18 + f * 2, 8)
        cv.fill(sp & ~body, P["glow"][0])
    cv.outline(P["o"])
    return cv


def hades_hands(f) -> Canvas:
    """Spectral hands claw up from the ground and drag men under."""
    P = gods.HADES
    cv = Canvas(E, E)
    rise = (6, 16, 26, 30, 22, 10)[f]
    pool = cv.ellipse(32, 56, 26, 6)
    cv.fill(pool, P["robe"][1])
    cv.fill(pool & ~cv.ellipse(32, 56, 22, 4), P["soul"][0])
    for k, (x, lean) in enumerate(((16, -4), (30, 2), (44, 5))):
        top = 56 - rise + k * 3
        arm = taper(cv, [(x, 58), (x + lean, top)], 2.4, ends=(2.6, 2))
        cv.fill(arm, P["soul"][1])
        cv.fill(arm & ~np.roll(arm, 1, axis=1), P["soul"][2])
        for j in range(4):
            cl = 2 if f in (2, 3) else 0
            cv.fill(cv.stroke([(x + lean - 2 + j * 1.5, top), (x + lean - 3 + j * 2 + cl, top - 5)], 1),
                    P["soul"][2])
    if f >= 2:
        for k in range(6):
            a = k + f
            cv.put(32 + math.cos(a) * 20, 40 - f * 3 + math.sin(a) * 6, P["soul"][2])
    cv.outline(P["o"])
    return cv


def ares_fire(f) -> Canvas:
    """The ground splits and a column of war-fire erupts."""
    P = gods.ARES
    cv = Canvas(E, E)
    h = (6, 20, 40, 48, 30, 12)[f]
    yy = np.mgrid[0:E, 0:E][0]
    crack = cv.stroke([(8, 58), (20, 55), (32, 59), (44, 54), (56, 58)], 2)
    cv.fill(crack, P["fire"][0])
    for k in range(5):
        x = 14 + k * 9
        fh = h * (0.6 + 0.4 * math.sin(k * 1.7 + f))
        fl = taper(cv, [(x, 58), (x + math.sin(f + k) * 3, 58 - fh)], 4 + (k % 2) * 2, ends=(4, 0))
        t = np.clip((58 - yy) / max(fh, 1), 0, 1)
        cv.tone(fl, [P["red"][2], P["fire"][0], P["fire"][1], P["fire"][2]][::-1], t, 1.0)
    if f >= 3:
        sm = (noise(E, E, 20 + f, 4.0, 2) > 0.55) & cv.ellipse(32, 20 - f * 2, 22, 10)
        cv.fill(sm & ~cv.opaque(), P["bronze"][0])
    for k in range(10):
        cv.put((k * 23 + f * 7) % 60 + 2, 50 - (k * 13 + f * 9) % 46, P["fire"][2])
    return cv


def kronos_rings(f) -> Canvas:
    """A shockwave of stopped time: a clock ring expands, men freeze."""
    P = gods.KRONOS
    cv = Canvas(E, E)
    r = (6, 12, 18, 24, 28, 30)[f]
    yy, xx = np.mgrid[0:E, 0:E]
    d = ((xx - 32) / r) ** 2 + ((yy - 40) / (r * 0.55)) ** 2
    band = (d <= 1) & (d >= (1 - 3 / r) ** 2)
    cv.fill(band, P["gold"][1] if f < 4 else P["gold"][0])
    ang = np.arctan2((yy - 40) / 0.55, xx - 32) + f * 0.3
    cv.fill(band & (np.mod(ang * 6 / math.pi, 1.0) < 0.25), P["gold"][2])
    if f <= 3:
        cv.fill(cv.ellipse(32, 40, r * 0.3, r * 0.17), P["sun"][1])
    hand = (-math.pi / 2 + f * 0.5)
    cv.fill(cv.stroke([(32, 40), (32 + math.cos(hand) * r * 0.8, 40 + math.sin(hand) * r * 0.45)], 1),
            P["star"][1])
    cv.outline(P["o"])
    return cv


EFFECTS = {"zeus": zeus_strike, "poseidon": poseidon_wave, "hades": hades_hands,
           "ares": ares_fire, "kronos": kronos_rings}


def sheet(god):
    fn = EFFECTS[god]
    cv = Canvas(E, E * FRAMES)
    for f in range(FRAMES):
        cv.paste(fn(f), 0, f * E)
    return cv

"""squads.py - the army as seen from the high battle camera.

The camera sits high above a 512x512 field, so a soldier is ~9 px tall and
one OAM sprite carries a whole block of men: a 32x16 squad holds 12 soldiers
in two ranks. 128 sprites -> ~1,500 soldiers on screen; the simulation owns
thousands more off-screen. Casualties thin the block (strength 12/9/6/3) so
losses are visible without one sprite per man.

Same civ palettes and role order as units.py (one 16-colour bank per civ).
"""

from __future__ import annotations

import numpy as np

from px import Canvas
import units

# Micro soldier, facing right: 5 px wide, 9 px tall (+ weapon). '.' clear.
# Rows: crest, helm, face, torso x3, tunic, legs x2. Shield sits in front.
BODY = {
    "hellenes": ["..a..", ".hh..", ".hs..", ".ccmh", ".ccmm", ".ccMm", ".lc..", "", ""],
    "rome":     [".....", ".hh..", ".hs..", ".hcch", ".hcca", ".hccc", ".cC..", "", ""],
    "carthage": [".l...", ".hh..", ".hs..", ".llaa", ".lcaa", ".ccaA", ".lc..", "", ""],
    "assyria":  ["..h..", ".hh..", ".os..", ".mcww", ".mcwW", ".ccwW", ".cC..", "", ""],
}
LEGS = [(".s.s.", ".s..s"), (".ss..", ".ss.."), (".s.s.", "s..s."), ("..ss.", "..ss.")]

STRENGTHS = (12, 9, 6, 3)
# file positions (x) and rank offsets; men are removed back-rank first,
# then from the flank, so a thinned block still reads as a formation.
SLOTS = [(x * 5 + 3, 0) for x in range(6)] + [(x * 5 + 1, -4) for x in range(6)]
KEEP = {12: list(range(12)), 9: [0, 1, 2, 3, 4, 5, 7, 9, 11], 6: [0, 1, 2, 3, 4, 5],
        3: [1, 3, 5]}


def soldier(cv, K, civ, x, y, cls, anim, f):
    """Draw one micro soldier with its feet on row y."""
    body = BODY[civ]
    legs = LEGS[f % 4] if anim == "walk" else LEGS[0]
    bob = 1 if anim == "walk" and f % 2 else 0
    top = y - 8 - bob
    rows = body[:7]
    cv.sprite(rows, K, x, top)
    cv.sprite(list(legs), K, x, y - 1)
    if cls == "spear":
        if anim == "attack" and f in (1, 2):
            reach = 4 if f == 1 else 2
            cv.line(x + 1, top + 4, x + 5 + reach, top + 4, K["W"])
            cv.put(x + 5 + reach, top + 4, K["*"])
        else:
            cv.line(x + 3, top - 3, x + 3, top + 6, K["W"])
            cv.put(x + 3, top - 4, K["*"])
    elif cls == "blade":
        if anim == "attack" and f == 1:
            cv.line(x + 4, top + 2, x + 7, top + 5, K["*"])
        else:
            cv.line(x + 4, top + 1, x + 4, top + 3, K["h"])
    elif cls == "archer":
        if anim == "attack":
            cv.line(x + 4, top + 1 - f, x + 4, top + 6 - f, K["w"])
            cv.put(x + 5, top + 3 - f, K["*"] if f < 2 else K["w"])
        else:
            cv.line(x, top + 3, x, top + 6, K["w"])


def archer_body(civ):
    b = BODY[civ][:]
    return [b[0], b[1], b[2], ".cc..", ".cc..", ".cC..", ".lc.."]


def squad_frame(civ, cls, anim, f, strength) -> Canvas:
    K = units.keys(civ)
    cv = Canvas(32, 16)
    keep = KEEP[strength]
    order = sorted(keep, key=lambda i: SLOTS[i][1])  # back rank first
    for i in order:
        sx, ry = SLOTS[i]
        # stagger the step so a block marches as many men, not one stamp
        fi = f + (i % 3) if anim == "walk" else f
        y = 15 + ry
        if cls == "archer":
            saved = BODY[civ]
            BODY[civ] = archer_body(civ) + ["", ""]
            soldier(cv, K, civ, sx, y, cls, anim, fi)
            BODY[civ] = saved
        else:
            soldier(cv, K, civ, sx, y, cls, anim, fi)
    _shadow_outline(cv, K)
    return cv


def _shadow_outline(cv, K):
    """Outline only below and behind: keeps tiny men from merging into mush
    on grass while staying 1 px wide on the lit side."""
    op = cv.opaque()
    ring = np.zeros_like(op)
    ring[1:] |= op[:-1]
    ring[:, :-1] |= op[:, 1:]
    ring &= ~op
    cv.fill(ring, K["o"])


# Cavalry: 4 riders per 32x16 block, horses 9x5 at the micro scale.
HORSE = {0: ["..oo.....", ".occccc..", "occcccco.", "o.c...c..", ".c.....c."],
         1: ["..oo.....", ".occccc..", "occcccco.", "..c..c...", "..c..c..."],
         2: ["..oo.....", ".occccc..", "occcccco.", ".c....c..", "c......c."],
         3: ["..oo.....", ".occccc..", "occcccco.", "..cc.cc..", "...c..c.."]}


def cavalry_frame(civ, anim, f, strength) -> Canvas:
    K = dict(units.keys(civ))
    coat = {"hellenes": "w", "rome": "s", "carthage": "l", "assyria": "W"}[civ]
    hk = dict(K)
    hk["c"] = K[coat]
    cv = Canvas(32, 16)
    riders = [(1, 15), (16, 15), (8, 11), (23, 11)][: max(1, strength // 3)]
    for n, (x, y) in enumerate(sorted(riders, key=lambda r: r[1])):
        g = (f + n) % 4 if anim == "walk" else (1 if anim == "attack" else 0)
        rows = [r[::-1] for r in HORSE[g]]  # face right
        cv.sprite(rows, hk, x, y - 4)
        cv.sprite(BODY[civ][:4], K, x + 2, y - 9)
        if anim == "attack" and f == 1:
            cv.line(x + 4, y - 6, x + 11, y - 7, K["W"])
            cv.put(x + 11, y - 7, K["*"])
        else:
            cv.line(x + 5, y - 12, x + 5, y - 5, K["W"])
            cv.put(x + 5, y - 13, K["*"])
    _shadow_outline(cv, K)
    return cv


def general_frame(civ, anim, f) -> Canvas:
    """16x16: the general rides with a standard bearer; the banner is the
    one big readable shape a player looks for in a crowd."""
    K = units.keys(civ)
    cv = Canvas(16, 16)
    sway = (0, 1, 0, -1)[f % 4]
    cv.line(3, 1, 3, 13, K["W"])
    flag = [(4, 1), (10 + sway, 2), (9 + sway, 4), (10 + sway, 6), (4, 6)]
    cv.fill(cv.poly(flag), K["a"])
    cv.fill(cv.rect(5, 3, 6, 4), K["*"])
    g = f % 4 if anim == "walk" else 0
    cv.sprite([r[::-1] for r in HORSE[g]], dict(K, c=K["A"]), 5, 11)
    cv.sprite(BODY[civ][:4], K, 7, 6)
    if anim == "command":
        cv.line(11, 7, 13, 2 - f % 2, K["*"])
    _shadow_outline(cv, K)
    return cv


# The fallen (GAME_DESIGN 4: death animations; 7: casualties you can see).
# Three frames: struck (the men drop to a knee), falling (pitched over)
# and down (lying flat, weapon dropped). The battle view plays the first
# two where a company falls and leaves the last on the ground, so a field
# fills with the dead of the fight that crossed it.
FALLEN = [(3, 15), (12, 11), (19, 15), (26, 12)]


def fallen_frame(civ, cls, f) -> Canvas:
    K = units.keys(civ)
    cv = Canvas(32, 16)
    if cls == "cavalry":
        for n, (x, y) in enumerate(FALLEN[::2]):
            x += n * 4
            if f < 2:
                rows = [r[::-1] for r in HORSE[0 if f == 0 else 2]]
                cv.sprite(rows, dict(K, c=K["W"]), x, y - 4 + f * 2)
                cv.sprite(BODY[civ][:4], K, x + 2 + f * 2, y - 9 + f * 3)
            else:
                # the horse on its side, legs out; the rider thrown clear
                cv.fill(cv.ellipse(x + 5, y - 1.5, 5, 1.6), K["W"])
                for lx in (x + 2, x + 4, x + 6, x + 8):
                    cv.line(lx, y - 3, lx + 1, y - 5, K["W"])
                cv.put(x + 10, y - 2, K["w"])
                cv.sprite(["hcs", "hcs"], K, x + 11, y - 1)
                cv.line(x - 1, y - 6, x + 4, y - 7, K["W"])
        _shadow_outline(cv, K)
        return cv
    body = BODY[civ] if cls != "archer" else archer_body(civ)
    for n, (x, y) in enumerate(FALLEN):
        if f == 0:
            # struck: down on one knee, the head bowed a pixel
            cv.sprite(body[1:6], K, x, y - 5)
            cv.sprite([".ss.."], K, x, y)
        elif f == 1:
            # falling: pitched back on a diagonal, the weapon flying
            for i, (dx, dy, k) in enumerate(((0, -4, "h"), (1, -3, "h"), (1, -2, "c"),
                                             (2, -2, "c"), (2, -1, "c"), (3, -1, "s"),
                                             (4, 0, "s"))):
                cv.put(x + dx, y + dy, K[k])
            cv.line(x + 3, y - 7, x + 6, y - 5, K["W"])
        else:
            # down: flat on the ground; spear or bow beside, a shield rim
            cv.sprite(["hhccss.", "Lhcccss"], K, x - 1, y - 1)
            if n % 2:
                cv.line(x - 2, y - 3, x + 5, y - 4, K["W"])
            else:
                cv.put(x + 6, y - 2, K["m"])
                cv.put(x + 7, y - 2, K["m"])
    _shadow_outline(cv, K)
    return cv


ANIMS = {"idle": 2, "walk": 4, "attack": 3}
FALLEN_FRAMES = 3


def squad_sheet(civ, cls) -> tuple:
    """Frames: for each strength (12, 9, 6, 3): idle 2, walk 4, attack 3."""
    frames, index = [], {}
    for s in STRENGTHS:
        for a, n in ANIMS.items():
            index[f"{a}_{s}"] = (len(frames), n)
            for f in range(n):
                if cls == "cavalry":
                    frames.append(cavalry_frame(civ, a, f, s))
                else:
                    frames.append(squad_frame(civ, cls, a, f, s))
    # one set of the fallen for every strength: the dead are not thinned
    index["death"] = (len(frames), FALLEN_FRAMES)
    frames += [fallen_frame(civ, cls, f) for f in range(FALLEN_FRAMES)]
    cv = Canvas(32 * len(frames), 16)
    for i, fr in enumerate(frames):
        cv.paste(fr, i * 32, 0)
    return cv, index


def general_sheet(civ) -> tuple:
    seq = [("idle", 2), ("walk", 4), ("command", 2)]
    frames, index = [], {}
    for a, n in seq:
        index[a] = (len(frames), n)
        frames += [general_frame(civ, a, f) for f in range(n)]
    cv = Canvas(16 * len(frames), 16)
    for i, fr in enumerate(frames):
        cv.paste(fr, i * 16, 0)
    return cv, index


SQUAD_CLASSES = ["spear", "blade", "archer", "cavalry"]

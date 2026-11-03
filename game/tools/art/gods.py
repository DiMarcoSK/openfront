"""gods.py - the gods as living entities on the battle map.

Each god is a 128x128 animated sprite (on the DS: four 64x64 OAM sprites
sharing one 16-colour palette, one frame streamed per tick), drawn by a rig:
a paint(pose) function plus pose tables per animation. They walk across
the high-camera field (~9 px soldiers -> a god is ~12x a man), attack,
cast a special, flinch and die. Attack payloads (bolt, wave, soul hands,
fire, time rings) live in effects.py.

All frames face right; the OAM h-flip turns them toward the other army.
"""

from __future__ import annotations

import math

import numpy as np
from PIL import Image

from px import Canvas, ramp, noise, dilate
from stage import taper, tube, bezier

S = 128
L = (-0.6, -0.7, 0.45)       # the map's light: top-left, like the terrain
ANIMS = ["idle", "walk", "attack", "special", "hurt", "death"]
COUNTS = dict(idle=4, walk=6, attack=6, special=6, hurt=2, death=6)


def limb(cv, x, y, ang, length, w0, w1):
    a = math.radians(ang)
    ex, ey = x + math.cos(a) * length, y + math.sin(a) * length
    return tube(cv, [(x, y), ((x + ex) / 2, (y + ey) / 2), (ex, ey)], w0, w1), (ex, ey)


def sh(cv, mask, colors, bevel=4, amb=0.15, bias=0.0, light=L, dither=0.4):
    cv.shade(mask, colors, light=light, bevel=bevel, dither=dither, ambient=amb, bias=bias)


def dissolve(cv, t, seed, edge):
    """Death: noise-thresholded erase; the dissolving edge glows."""
    if t <= 0:
        return
    n = noise(S, S, seed, 5.0, 3)
    op = cv.opaque()
    gone = op & (n < t)
    rim = op & ~gone & (n < t + 0.06)
    cv.erase(gone)
    cv.fill(rim, edge)
    rng = np.random.default_rng(seed + int(t * 100))
    ys, xs = np.nonzero(gone)
    for i in rng.choice(len(xs), size=min(len(xs), 40), replace=False) if len(xs) else []:
        cv.put(xs[i] + rng.integers(-2, 3), ys[i] - int(t * 20) - rng.integers(0, 6), edge)


def scale_canvas(cv, k):
    """Uniform shrink about the centre (Kronos implodes)."""
    img = cv.image()
    w = max(1, int(S * k))
    small = img.resize((w, w), Image.NEAREST)
    out = Canvas(S, S)
    tmp = Canvas(w, w)
    tmp.a[:] = np.array(small)
    out.paste(tmp, (S - w) // 2, (S - w) // 2 + int((1 - k) * 20))
    return out


# ---------------------------------------------------------------------------
# ZEUS - rides a storm cloud, hurls the keraunos.
# ---------------------------------------------------------------------------
ZEUS = dict(
    o=ramp("#0a0c1c")[0],
    skin=ramp("#2a2238", "#56486a", "#9a86a0", "#e0cfc8"),
    cloud=ramp("#121830", "#26325a", "#44568a"),
    white=ramp("#8ea6cc", "#d6e4f4", "#ffffff"),
    bolt=ramp("#2f6bd8", "#6fd8ff"),
    gold=ramp("#f6d878"),
)


def zeus_bolt(cv, cx, cy, ang, P, length=34):
    a = math.radians(ang)
    ux, uy = math.cos(a), math.sin(a)
    px_, py_ = -uy, ux
    pts = [(cx + ux * t * length + px_ * side, cy + uy * t * length + py_ * side)
           for t, side in ((-0.5, 0), (-0.25, 4), (-0.05, -4), (0.15, 4), (0.32, -3), (0.5, 0))]
    b = taper(cv, pts, 4.2)
    cv.fill(dilate(dilate(b, True), True) & ~b & ~cv.opaque(), P["bolt"][0])
    cv.fill(b, P["bolt"][1])
    cv.fill(taper(cv, pts, 2.0), P["white"][2])


def zeus(p) -> Canvas:
    P = ZEUS
    cv = Canvas(S, S)
    bob, lean, ph = p.get("bob", 0), p.get("lean", 0), p.get("phase", 0.0)
    sx, sy = 62 + lean, 44 + bob
    # hair streaming back in the storm wind
    for k in range(6):
        hl = taper(cv, [(sx + 4, sy - 26 + k * 2), (sx - 14 - k * 2, sy - 30 + k * 3 + math.sin(ph + k) * 2),
                        (sx - 32 - k * 3, sy - 22 + k * 4 + math.sin(ph * 1.5 + k) * 3)], 3.4)
        sh(cv, hl, P["white"], bevel=2, amb=0.25)
    # far arm
    fa, fe = p.get("far", (120, 60))
    m, (ex, ey) = limb(cv, sx - 22, sy + 2, fa, 17, 6, 5)
    sh(cv, m, P["skin"][:3], bevel=3, amb=0.1)
    m2, (hx, hy) = limb(cv, ex, ey, fe, 15, 5, 4)
    sh(cv, m2, P["skin"][:3], bevel=3, amb=0.1)
    # back of the storm cloud
    cloud_back = np.zeros((S, S), bool)
    for k in range(6):
        a = k / 6 * 2 * math.pi + ph
        cloud_back |= cv.ellipse(60 + math.cos(a) * 32, 102 + math.sin(a) * 6 - 4, 14, 10)
    sh(cv, cloud_back, P["cloud"], bevel=4, amb=0.2)
    # torso: broad shoulders, carved chest and abdomen
    torso = cv.poly([(sx - 26, sy - 2), (sx + 24, sy - 2), (sx + 18, sy + 48), (sx - 20, sy + 48)])
    torso |= cv.ellipse(sx - 24, sy + 4, 9, 8) | cv.ellipse(sx + 22, sy + 3, 9, 8)
    sh(cv, torso, P["skin"], bevel=9, amb=0.1)
    for side in (-1, 1):
        sh(cv, cv.ellipse(sx + side * 10, sy + 12, 10, 7) & torso, P["skin"], bevel=3, amb=0.15)
    cv.fill(cv.stroke([(sx - 18, sy + 18), (sx - 2, sy + 21), (sx + 16, sy + 17)], 1) & torso, P["skin"][0])
    cv.fill(cv.stroke([(sx, sy + 8), (sx, sy + 46)], 1) & torso, P["skin"][1])
    for y in (sy + 28, sy + 36):
        cv.fill(cv.stroke([(sx - 8, y), (sx + 8, y)], 1) & torso, P["skin"][1])
    # himation: draped from the far shoulder to the hip, chest bare
    him = cv.poly([(sx - 30, sy - 4), (sx - 16, sy - 4), (sx - 8, sy + 26), (sx + 22, sy + 36),
                   (sx + 20, sy + 50), (sx - 22, sy + 50)])
    sh(cv, him & (torso | cv.ellipse(sx - 24, sy + 4, 10, 9)), P["white"], bevel=3, amb=0.2)
    for k in range(4):
        cv.fill(cv.stroke([(sx - 22 + k * 7, sy + 30 + k), (sx - 14 + k * 9, sy + 50)], 1) & him, P["white"][0])
    # head, beard, laurel, burning eye
    hx_, hy_ = sx + 4, sy - 20
    head = cv.ellipse(hx_, hy_, 11, 13)
    sh(cv, head, P["skin"], bevel=6, amb=0.12)
    cv.fill(cv.poly([(hx_ - 2, hy_ - 5), (hx_ + 11, hy_ - 6), (hx_ + 11, hy_ - 3), (hx_ - 1, hy_ - 2)]), P["skin"][1])
    eye = P["white"][2] if p.get("flare") else P["bolt"][1]
    cv.fill(cv.rect(hx_ + 3, hy_ - 2, hx_ + 8, hy_ - 1), eye)
    cv.put(hx_ + 6, hy_ - 2, P["white"][2])
    cv.fill(cv.poly([(hx_ + 10, hy_ - 2), (hx_ + 14, hy_ + 3), (hx_ + 10, hy_ + 4)]), P["skin"][2])
    beard = cv.poly([(hx_ - 10, hy_ + 2), (hx_ + 11, hy_ + 4), (hx_ + 10, hy_ + 20), (hx_ + 2, hy_ + 34),
                     (hx_ - 8, hy_ + 24)])
    sh(cv, beard, P["white"], bevel=3, amb=0.2)
    for k in range(4):
        cv.fill(cv.stroke([(hx_ - 6 + k * 5, hy_ + 8), (hx_ - 5 + k * 4, hy_ + 26 - abs(k - 1.5) * 4)], 1)
                & beard, P["white"][0])
    cv.fill(cv.rect(hx_ + 4, hy_ + 6, hx_ + 10, hy_ + 7), P["cloud"][1])
    for i in range(-5, 6):
        cv.put(hx_ + i * 2, hy_ - 11 + abs(i) * 0.5 - (1 if i % 2 else 0), P["gold"][0])
    # front of the storm cloud: dark, rolling, lit on its crown
    cloud = np.zeros((S, S), bool)
    for k in range(7):
        a = k / 7 * 2 * math.pi + ph
        cloud |= cv.ellipse(60 + math.cos(a) * 36, 110 + math.sin(a) * 7, 13 + (k % 2) * 3, 9)
    cloud |= cv.ellipse(sx - 2, sy + 52, 28, 10)
    sh(cv, cloud, P["cloud"], bevel=4, amb=0.25)
    cv.fill(cloud & ~np.roll(cloud, 1, axis=0), P["white"][0])
    if int(ph * 3) % 3 == 0:
        fl = [(30 + (ph * 40) % 60, 104), (36 + (ph * 40) % 60, 110), (32 + (ph * 40) % 60, 114)]
        cv.fill(cv.stroke(fl, 1) & cloud, P["bolt"][1])
    # near arm with the bolt
    a1, a2 = p.get("arm", (-60, -110))
    m, (ex, ey) = limb(cv, sx + 22, sy + 2, a1, 19, 7.5, 6)
    sh(cv, m, P["skin"], bevel=3, amb=0.12)
    m2, (hx, hy) = limb(cv, ex, ey, a2, 16, 6, 4.5)
    sh(cv, m2, P["skin"], bevel=3, amb=0.15)
    fist = cv.ellipse(hx, hy, 5, 5)
    sh(cv, fist, P["skin"], bevel=2, amb=0.2)
    if p.get("bolt", True):
        zeus_bolt(cv, hx, hy, a2 + 90, P)
    aura = p.get("aura", 0)
    if aura:
        rng = np.random.default_rng(int(aura * 97))
        for _ in range(int(aura * 6)):
            x0, y0 = rng.uniform(10, 118), rng.uniform(2, 90)
            pts = [(x0, y0)]
            for _ in range(4):
                x0 += rng.uniform(-6, 6)
                y0 += rng.uniform(2, 7)
                pts.append((x0, y0))
            cv.fill(cv.stroke(pts, 1), P["bolt"][1])
            cv.put(*pts[0], P["white"][2])
    cv.outline(P["o"])
    dissolve(cv, p.get("die", 0), 11, P["white"][1])
    return cv


def zeus_poses():
    out = {}
    out["idle"] = [dict(bob=b, phase=k * 0.4) for k, b in enumerate((0, -1, -1, 0))]
    out["walk"] = [dict(bob=(0, -1, -2, -1, 0, 1)[k], lean=2, phase=k * 1.05) for k in range(6)]
    arms = [(-150, -100), (-165, -125), (-60, -30), (-10, 0), (20, 30), (-40, -70)]
    leans = [-2, -3, 3, 5, 4, 1]
    out["attack"] = [dict(arm=arms[k], lean=leans[k], bolt=k < 3, phase=k * 0.3) for k in range(6)]
    out["special"] = [dict(arm=(-100, -95), far=(-95, -100), aura=0.4 + k * 0.12, flare=True,
                           bob=-1 - (k % 2), phase=k * 0.6) for k in range(6)]
    out["hurt"] = [dict(lean=-5, arm=(-20, -60), far=(150, 120)), dict(lean=-3, arm=(-40, -90))]
    out["death"] = [dict(lean=-4, bob=k * 2, die=0.12 + k * 0.16, phase=k * 0.5) for k in range(6)]
    return out


# ---------------------------------------------------------------------------
# POSEIDON - the leviathan swims the field.
# ---------------------------------------------------------------------------
POSEIDON = dict(
    o=ramp("#020a10")[0],
    hide=ramp("#082028", "#124048", "#1f6660", "#3a8f7c", "#7cc8a8"),
    belly=ramp("#4e5a3c", "#9aa476", "#e2e4bc"),
    crown=ramp("#7a2c14", "#d06a34", "#ffc890"),
    glow=ramp("#2ad8c0", "#e8fffa"),
)


def poseidon(p) -> Canvas:
    P = POSEIDON
    cv = Canvas(S, S)
    ph = p.get("phase", 0.0)
    sink = p.get("sink", 0)
    hx, hy = p.get("head", (92, 34))
    jaw = p.get("jaw", 4)
    wl = 112                                   # water line
    # back coil hump, rolling
    hump = tube(cv, bezier((96, wl + 4), (102, wl - 18 - math.sin(ph) * 4), (120, wl - 18 - math.sin(ph) * 4),
                           (126, wl + 4), 12), 6, 6)
    sh(cv, hump, P["hide"][:4], bevel=4, amb=0.1)
    base = (36, wl + 6)
    sway = math.sin(ph) * 14
    spine = bezier(base, (30 + sway, wl - 40), (hx - 40 - sway, hy + 10), (hx - 8, hy + 4), 24)
    spine = [(x, y + sink) for x, y in spine]
    hx, hy = hx, hy + sink
    neck = tube(cv, spine, 13, 9)
    # dorsal fins on the outer curve
    for i in range(3, 22, 3):
        x, y = spine[i]
        ax, ay = spine[i + 1]
        tx, ty = ax - x, ay - y
        ln = math.hypot(tx, ty) or 1
        tx, ty = tx / ln, ty / ln
        nx, ny = ty, -tx
        if ny > 0:
            nx, ny = -nx, -ny
        w = 13 - 4 * i / 24
        bx, by = x + nx * (w - 1), y + ny * (w - 1)
        fin = cv.poly([(bx - tx * 3, by - ty * 3), (bx + tx * 3, by + ty * 3),
                       (bx + nx * 9 - tx * 5, by + ny * 9 - ty * 5)])
        cv.fill(fin, P["crown"][1])
        cv.fill(fin & ~np.roll(fin, 1, axis=1), P["crown"][2])
    sh(cv, neck, P["hide"], bevel=7, amb=0.12)
    belly = tube(cv, spine, 13, 9, side=-1) & ~tube(cv, spine, 7, 4, side=-1)
    sh(cv, belly & neck, P["belly"], bevel=2, amb=0.2)
    for i in range(0, 24, 2):
        x, y = spine[i]
        cv.fill(cv.stroke([(x, y), (x + 8, y + 6)], 1) & belly & neck, P["belly"][0])
    # scales: tiny arcs on the hide
    scl = neck & ~belly & (((np.mgrid[0:S, 0:S][0] + (np.mgrid[0:S, 0:S][1] // 3) % 2 * 2) % 4) == 0) & \
        ((np.mgrid[0:S, 0:S][1] % 3) != 0)
    cv.fill(scl, P["hide"][1])
    # head: snout to the right, jaw opens by `jaw`
    skull = cv.poly([(hx - 12, hy - 8), (hx + 2, hy - 10), (hx + 20, hy - 3), (hx + 30, hy + 3),
                     (hx + 28, hy + 6), (hx + 6, hy + 6), (hx - 10, hy + 8)])
    lower = cv.poly([(hx - 6, hy + 6), (hx + 26, hy + 6 + jaw), (hx + 24, hy + 10 + jaw), (hx - 4, hy + 13)])
    if jaw > 2:
        maw = cv.poly([(hx - 2, hy + 6), (hx + 28, hy + 5), (hx + 26, hy + 7 + jaw), (hx - 2, hy + 10)])
        cv.fill(maw, P["crown"][0])
        for k in range(6):
            x = hx + 4 + k * 4
            cv.fill(cv.poly([(x, hy + 6), (x + 2, hy + 6), (x + 1, hy + 9)]), P["belly"][2])
            cv.fill(cv.poly([(x + 1, hy + 6 + jaw + 2), (x + 3, hy + 6 + jaw + 2), (x + 2, hy + 4 + jaw)]),
                    P["belly"][1])
    sh(cv, lower, P["hide"][:4], bevel=2, amb=0.15)
    sh(cv, skull, P["hide"], bevel=4, amb=0.15)
    for k, ex in enumerate((hx + 4, hx + 10, hx + 15)):
        cv.fill(cv.ellipse(ex, hy - 3 + k * 0.6, 2 - k * 0.3, 1.5), P["glow"][0])
        cv.put(ex, hy - 3 + k * 0.6, P["glow"][1])
    # trident crown
    for dx, h in ((-6, 14), (0, 20), (6, 14)):
        tine = taper(cv, [(hx + dx, hy - 7), (hx + dx - 3, hy - 7 - h)], 2.2, ends=(2.2, 0))
        cv.fill(tine, P["crown"][1])
        cv.fill(tine & ~np.roll(tine, 1, axis=1), P["crown"][2])
    cv.fill(cv.rect(hx - 8, hy - 9, hx + 8, hy - 7), P["crown"][0])
    cv.put(hx, hy - 8, P["glow"][1])
    if p.get("roar"):
        for k in range(3):
            r = 6 + k * 5 + p["roar"] * 2
            ring = cv.ellipse(hx + 34, hy + 8, r, r * 0.8) & ~cv.ellipse(hx + 34, hy + 8, r - 1, r * 0.8 - 1)
            cv.fill(ring & cv.rect(hx + 30, 0, S, S), P["glow"][0])
    cv.outline(P["o"])
    # the sea he swims in: churn round the neck, foam rings rolling
    water = cv.rect(0, wl + 2, S - 1, S - 1)
    cv.erase(water & ~cv.ellipse(36, wl + 6, 26, 8) & ~cv.ellipse(110, wl + 6, 20, 6))
    for cx_, rx in ((36, 24), (110, 18)):
        ring = cv.ellipse(cx_, wl + 6, rx, 6) & ~cv.ellipse(cx_, wl + 6, rx - 3, 4)
        cv.fill(ring, P["hide"][2])
        for k in range(8):
            a = k / 8 * 2 * math.pi + ph * 1.5
            cv.put(cx_ + math.cos(a) * (rx - 1), wl + 6 + math.sin(a) * 5, P["glow"][1])
        inner = cv.ellipse(cx_, wl + 6, rx - 3, 4)
        cv.fill(inner & ~cv.opaque() | (inner & (np.mgrid[0:S, 0:S][0] > wl + 4)), P["hide"][1])
    if sink:
        sp = (noise(S, S, 5, 3.0, 2) > 0.68) & cv.ellipse(40, wl - 2, 22, 6 + sink // 6) & ~cv.opaque()
        cv.fill(sp, P["glow"][1])
    return cv


def poseidon_poses():
    out = {}
    out["idle"] = [dict(phase=k * 0.5, head=(92, 34 + (0, 1, 2, 1)[k])) for k in range(4)]
    out["walk"] = [dict(phase=k * 1.05, head=(94 + math.sin(k * 1.05) * 3, 36 + math.cos(k * 1.05) * 3))
                   for k in range(6)]
    heads = [(80, 26), (70, 20), (98, 60), (100, 66), (96, 56), (90, 38)]
    jaws = [4, 8, 12, 2, 0, 3]
    out["attack"] = [dict(head=heads[k], jaw=jaws[k], phase=k * 0.4) for k in range(6)]
    out["special"] = [dict(head=(84, 16 - k), jaw=10, roar=k, phase=k * 0.8) for k in range(6)]
    out["hurt"] = [dict(head=(78, 30), jaw=6), dict(head=(84, 32), jaw=3)]
    out["death"] = [dict(head=(92 - k * 3, 34 + k * 4), jaw=6, sink=k * 14, phase=k * 0.6) for k in range(6)]
    return out


# ---------------------------------------------------------------------------
# HADES - glides on a hem of souls, the bident in hand.
# ---------------------------------------------------------------------------
HADES = dict(
    o=ramp("#040208")[0],
    robe=ramp("#0c0814", "#1a1228", "#2c1f44", "#46346a"),
    bone=ramp("#4c4452", "#8a8078", "#cfc6b4", "#fbf6e6"),
    soul=ramp("#1d6e5a", "#3aae86", "#9affd0"),
    gold=ramp("#6a4a18", "#e0b456"),
    ember=ramp("#c4401c")[0],
)


def hades(p) -> Canvas:
    P = HADES
    cv = Canvas(S, S)
    bob, ph, lean = p.get("bob", 0), p.get("phase", 0.0), p.get("lean", 0)
    cx, top = 60 + lean, 22 + bob
    # trailing wisps of the hem
    for k in range(5):
        y = top + 70 + k * 5
        w = taper(cv, [(cx - 18 + k * 6, y), (cx - 34 - k * 3, y + 6 + math.sin(ph + k) * 3),
                       (cx - 54 - k * 4, y + 2 + math.sin(ph * 1.3 + k) * 5)], 3)
        cv.fill(w, P["soul"][0] if k % 2 else P["robe"][2])
    # robe: a tall bell, folds carved
    robe = cv.poly([(cx - 10, top + 6), (cx + 12, top + 6), (cx + 30, top + 60), (cx + 26, top + 84),
                    (cx - 26, top + 84), (cx - 24, top + 52)])
    hood = cv.ellipse(cx + 2, top + 12, 14, 15)
    sh(cv, robe | hood, P["robe"], bevel=8, amb=0.1)
    for k in range(4):
        cv.fill(cv.stroke(bezier((cx - 6 + k * 6, top + 26), (cx - 10 + k * 8, top + 50),
                                 (cx - 14 + k * 10, top + 70), (cx - 20 + k * 13, top + 84), 10), 1) & robe,
                P["robe"][0])
    hem = robe & (np.mgrid[0:S, 0:S][0] > top + 78)
    rag = noise(S, S, 3, 3.0, 2) > 0.5
    cv.erase(hem & rag)
    cv.fill(dilate(hem & ~rag) & robe & (np.mgrid[0:S, 0:S][0] > top + 76), P["soul"][1])
    # skull in the hood
    hollow = cv.ellipse(cx + 4, top + 14, 9, 10)
    cv.fill(hollow, P["robe"][0])
    skull = cv.ellipse(cx + 5, top + 13, 6, 7) | cv.rect(cx + 2, top + 16, cx + 9, top + 21)
    sh(cv, skull, P["bone"], bevel=3, amb=0.2)
    cv.fill(cv.ellipse(cx + 7, top + 12, 1.8, 1.6), P["soul"][2] if p.get("flare") else P["soul"][1])
    cv.put(cx + 8, top + 12, P["bone"][3])
    cv.fill(cv.rect(cx + 3, top + 19, cx + 9, top + 19), P["bone"][0])
    for k in range(4):
        cv.put(cx + 3 + k * 2, top + 20, P["bone"][3])
    # crown with soul-fire
    for k in range(5):
        x = cx - 2 + k * 3.5
        hgt = 6 + (2 - abs(k - 2)) * 2
        cv.fill(cv.poly([(x - 1.5, top + 3), (x + 1.5, top + 3), (x, top + 3 - hgt)]), P["gold"][k % 2])
        fl = 2 + int((math.sin(ph * 2 + k) + 1) * 1.5)
        cv.fill(cv.rect(int(x), int(top + 2 - hgt - fl), int(x), int(top + 2 - hgt)), P["soul"][2])
    cv.fill(cv.rect(cx - 4, top + 2, cx + 13, top + 4), P["gold"][1])
    # far hand: raised for the special
    if p.get("raise"):
        m, (ex, ey) = limb(cv, cx - 10, top + 30, -120, 18, 2.5, 2)
        cv.fill(m, P["bone"][1])
        for k in range(4):
            cv.fill(cv.stroke([(ex, ey), (ex - 4 + k * 2.5, ey - 6)], 1), P["bone"][2])
        cv.fill(cv.ellipse(ex - 1, ey - 8, 5, 5) & ~cv.ellipse(ex - 1, ey - 8, 3, 3), P["soul"][1])
    # bident: angle from the pose (90 = upright, 0 = thrust right)
    ba = math.radians(-p.get("bident", 80))
    reach = p.get("reach", 0)
    hx, hy = cx + 18 + reach, top + 40
    ux, uy = math.cos(ba), math.sin(ba)
    cv.fill(cv.stroke([(hx - ux * 30, hy - uy * 30), (hx + ux * 30, hy + uy * 30)], 2), P["robe"][3])
    tip_x, tip_y = hx + ux * 30, hy + uy * 30
    for side in (-1, 1):
        nx, ny = -uy * side * 3, ux * side * 3
        cv.fill(cv.stroke([(tip_x + nx, tip_y + ny), (tip_x + nx + ux * 9, tip_y + ny + uy * 9)], 1), P["bone"][3])
        cv.fill(cv.stroke([(tip_x - ux, tip_y - uy), (tip_x + nx, tip_y + ny)], 1), P["bone"][2])
    cv.put(tip_x + ux * 4, tip_y + uy * 4, P["soul"][2])
    # near hand on the shaft
    m, _ = limb(cv, cx + 12, top + 28, math.degrees(math.atan2(hy - top - 28, hx - cx - 12)),
                math.hypot(hx - cx - 12, hy - top - 28), 3.5, 2.5)
    sh(cv, m, P["robe"][1:], bevel=2, amb=0.2)
    cv.fill(cv.ellipse(hx, hy, 2.5, 2.5), P["bone"][2])
    # souls orbiting
    for k in range(3):
        a = ph * 1.3 + k * 2.1
        x, y = cx + math.cos(a) * 34, top + 44 + math.sin(a) * 12
        cv.fill(cv.ellipse(x, y, 2, 2.4), P["soul"][2])
        cv.fill(cv.stroke([(x, y + 2), (x - 4, y + 7)], 1), P["soul"][0])
    cv.outline(P["o"])
    dissolve(cv, p.get("die", 0), 23, P["soul"][2])
    return cv


def hades_poses():
    out = {}
    out["idle"] = [dict(bob=(0, -1, -2, -1)[k], phase=k * 0.6) for k in range(4)]
    out["walk"] = [dict(bob=(0, -1, -2, -2, -1, 0)[k], lean=2, phase=k * 1.05, bident=70) for k in range(6)]
    bid = [100, 120, 10, 0, 0, 50]
    reach = [-4, -8, 8, 14, 10, 2]
    out["attack"] = [dict(bident=bid[k], reach=reach[k], lean=(-2, -3, 3, 5, 4, 1)[k], phase=k * 0.4)
                     for k in range(6)]
    out["special"] = [dict(raise_=True, flare=True, bob=-2 - k % 2, phase=k * 0.8, bident=95) for k in range(6)]
    for d in out["special"]:
        d["raise"] = d.pop("raise_")
    out["hurt"] = [dict(lean=-5, bident=110), dict(lean=-3, bident=95)]
    out["death"] = [dict(bob=k * 2, die=0.12 + k * 0.16, phase=k * 0.5) for k in range(6)]
    return out


# ---------------------------------------------------------------------------
# ARES - the bronze giant walks with a real stride.
# ---------------------------------------------------------------------------
ARES = dict(
    o=ramp("#140804")[0],
    bronze=ramp("#3c1a08", "#7a3e12", "#b8702a", "#e8b050", "#fff0b4"),
    flesh=ramp("#4a2418", "#8a5038", "#c08060"),
    red=ramp("#4a0a0e", "#8c1a18", "#d03c2a"),
    fire=ramp("#f47c1c", "#ffc04a", "#fff2a8"),
)

STRIDE = [  # (near thigh, near shin, far thigh, far shin) degrees from +x
    (55, 95, 120, 105), (72, 100, 108, 135), (95, 120, 85, 100),
    (122, 105, 55, 95), (108, 135, 72, 100), (85, 100, 95, 120),
]


def ares(p) -> Canvas:
    P = ARES
    cv = Canvas(S, S)
    bob, lean, ph = p.get("bob", 0), p.get("lean", 0), p.get("phase", 0.0)
    legs = p.get("legs", (80, 95, 100, 95))
    kneel = p.get("kneel", 0)
    hx, hy = 62 + lean, 68 + bob + kneel
    # cloak snapping behind
    cloak = cv.poly([(hx - 10, hy - 36), (hx + 8, hy - 36), (hx - 20, hy + 14),
                     (hx - 44 - math.sin(ph) * 4, hy + 8), (hx - 36, hy - 10)])
    sh(cv, cloak, P["red"], bevel=3, amb=0.2)
    # far leg, then far arm with the spear
    def leg(thigh, shin, near):
        m1, (kx, ky) = limb(cv, hx + (2 if near else -4), hy, thigh, 22 - kneel * 0.3, 6, 5)
        sh(cv, m1, P["flesh"] if near else P["flesh"][:2], bevel=3, amb=0.15)
        m2, (fx, fy) = limb(cv, kx, ky, shin, 22, 5, 4)
        sh(cv, m2, P["bronze"] if near else P["bronze"][:3], bevel=2, amb=0.15)
        cv.fill(cv.poly([(fx - 4, fy - 1), (fx + 7, fy - 1), (fx + 7, fy + 2), (fx - 4, fy + 2)]), P["bronze"][0])
        cv.fill(cv.ellipse(kx, ky, 4, 3), P["bronze"][2 if near else 1])
    leg(legs[2], legs[3], False)
    sa = p.get("spear", -95)
    sx_, sy_ = hx - 14, hy - 30
    m, (ex, ey) = limb(cv, sx_, sy_, p.get("farm", 70), 14, 5, 4)
    sh(cv, m, P["flesh"][:2], bevel=2, amb=0.2)
    m2, (gx, gy) = limb(cv, ex, ey, p.get("farm2", 20), 12, 4, 3.5)
    sh(cv, m2, P["flesh"][:2], bevel=2, amb=0.2)
    a = math.radians(sa)
    ux, uy = math.cos(a), math.sin(a)
    shaft = cv.stroke([(gx - ux * 26, gy - uy * 26), (gx + ux * 46, gy + uy * 46)], 2)
    cv.fill(shaft, P["bronze"][1])
    tip = (gx + ux * 46, gy + uy * 46)
    blade = cv.poly([(tip[0] + ux * 9, tip[1] + uy * 9), (tip[0] - uy * 3, tip[1] + ux * 3),
                     (tip[0] + uy * 3, tip[1] - ux * 3)])
    cv.fill(blade, P["bronze"][3])
    cv.put(tip[0] + ux * 6, tip[1] + uy * 6, P["bronze"][4])
    cv.fill(cv.ellipse(gx, gy, 3.5, 3.5), P["flesh"][1])
    leg(legs[0], legs[1], True)
    # skirt of pteruges
    for k in range(6):
        st = cv.rect(hx - 13 + k * 5, hy - 4, hx - 10 + k * 5, hy + 6 + (k % 2) * 2)
        cv.fill(st, P["red"][1] if k % 2 else P["red"][0])
    # cuirass
    torso = cv.poly([(hx - 16, hy - 38), (hx + 16, hy - 38), (hx + 13, hy - 18), (hx + 11, hy - 4),
                     (hx - 11, hy - 4), (hx - 13, hy - 18)])
    sh(cv, torso, P["bronze"], bevel=6, amb=0.12)
    cv.fill(cv.stroke([(hx - 12, hy - 24), (hx, hy - 21), (hx + 12, hy - 24)], 1) & torso, P["bronze"][1])
    cv.fill(cv.stroke([(hx, hy - 34), (hx, hy - 6)], 1) & torso, P["bronze"][1])
    cv.fill(cv.rect(hx - 12, hy - 6, hx + 12, hy - 4), P["bronze"][1])
    for side in (-1, 1):
        sh(cv, cv.ellipse(hx + side * 17, hy - 36, 7, 6), P["bronze"], bevel=3, amb=0.15)
    # helm with burning crest
    hdx, hdy = hx + 2, hy - 50
    crest = taper(cv, [(hdx - 16, hdy - 2), (hdx - 6, hdy - 13), (hdx + 8, hdy - 13), (hdx + 14, hdy - 6)],
                  4.5 + p.get("rage", 0), ends=(2, 1))
    fl = noise(S, S, 7 + int(ph * 10), 3.0, 2)
    cv.tone(crest, P["fire"], np.clip(fl * 1.2 - 0.1, 0, 1), 1.0)
    for k in range(4 + p.get("rage", 0)):
        x = hdx - 10 + k * 6
        cv.fill(taper(cv, [(x, hdy - 12), (x - 3 + math.sin(ph * 3 + k) * 2, hdy - 20 - (k % 2) * 4
                           - p.get("rage", 0) * 2)], 1.6, ends=(1.6, 0)), P["fire"][k % 3])
    helm = cv.ellipse(hdx, hdy, 9, 10) | cv.poly([(hdx - 8, hdy), (hdx + 9, hdy), (hdx + 7, hdy + 11),
                                                   (hdx - 6, hdy + 11)])
    sh(cv, helm, P["bronze"], bevel=4, amb=0.15)
    cv.fill(cv.poly([(hdx + 1, hdy - 1), (hdx + 9, hdy - 2), (hdx + 9, hdy + 1), (hdx + 2, hdy + 2)]), P["fire"][1])
    cv.put(hdx + 6, hdy - 1, P["fire"][2])
    cv.fill(cv.rect(hdx + 5, hdy + 2, hdx + 6, hdy + 10), P["bronze"][0])
    # the aspis on the near arm, the war-eye
    shx, shy = hx + 14 + p.get("bash", 0), hy - 20
    shield = cv.ellipse(shx, shy, 15, 16)
    cv.fill(dilate(shield, True) & ~shield, P["o"])
    sh(cv, shield, P["bronze"], bevel=3, amb=0.15)
    sh(cv, cv.ellipse(shx, shy, 11, 12), P["red"], bevel=4, amb=0.25)
    eye = cv.poly([(shx - 8, shy), (shx - 3, shy - 4), (shx + 4, shy - 4), (shx + 8, shy), (shx + 3, shy + 3),
                   (shx - 3, shy + 3)])
    cv.fill(eye, P["bronze"][4])
    cv.fill(cv.ellipse(shx, shy - 0.5, 2.6, 2.6), P["fire"][0])
    cv.fill(cv.ellipse(shx, shy - 0.5, 1.2, 1.6), P["o"])
    # ground fire in the special
    if p.get("rage"):
        for k in range(6):
            x = 20 + k * 18
            fh = 6 + p["rage"] * 3 + (k % 2) * 4
            fm = taper(cv, [(x, 126), (x + math.sin(ph + k) * 2, 126 - fh)], 3, ends=(3, 0))
            cv.tone(fm, P["fire"], np.clip((126 - np.mgrid[0:S, 0:S][0]) / max(fh, 1), 0, 1), 1.0)
    cv.outline(P["o"])
    dissolve(cv, p.get("die", 0), 37, P["fire"][1])
    return cv


def ares_poses():
    out = {}
    out["idle"] = [dict(bob=(0, 1, 1, 0)[k], phase=k * 0.6) for k in range(4)]
    out["walk"] = [dict(legs=STRIDE[k], bob=(0, -1, -2, 0, -1, -2)[k], lean=2, phase=k * 1.05) for k in range(6)]
    spear = [-150, -160, -10, 0, -5, -60]
    farm = [160, 170, 20, 10, 15, 100]
    farm2 = [120, 140, 0, -5, 0, 60]
    leans = [-3, -4, 4, 7, 5, 1]
    out["attack"] = [dict(spear=spear[k], farm=farm[k], farm2=farm2[k], lean=leans[k], phase=k * 0.4,
                          legs=(70, 100, 110, 100) if k in (2, 3, 4) else (85, 95, 95, 95)) for k in range(6)]
    out["special"] = [dict(rage=1 + k // 2, bash=(0, 4, 6, 6, 4, 2)[k], bob=(0, -2, 1, 1, 0, 0)[k], phase=k * 0.7,
                           spear=-100) for k in range(6)]
    out["hurt"] = [dict(lean=-6, bob=2), dict(lean=-3, bob=1)]
    out["death"] = [dict(kneel=min(12, k * 5), lean=k, legs=(60, 160, 100, 170), die=max(0, (k - 1) * 0.2),
                         phase=k * 0.5) for k in range(6)]
    return out


# ---------------------------------------------------------------------------
# KRONOS - drifts in a ring of time, the harpe sweeping.
# ---------------------------------------------------------------------------
KRONOS = dict(
    o=ramp("#04020a")[0],
    void=ramp("#0a0616", "#160c2c", "#241546", "#36205e"),
    stone=ramp("#4a4250", "#8a7e86", "#d0c4bc"),
    gold=ramp("#6a4614", "#c08a2c", "#ffe08a"),
    sun=ramp("#ff8a1c", "#fff0b0"),
    star=ramp("#a090e0", "#ffffff"),
)


def kronos(p) -> Canvas:
    P = KRONOS
    cv = Canvas(S, S)
    bob, ph, lean = p.get("bob", 0), p.get("phase", 0.0), p.get("lean", 0)
    cx, cy = 62 + lean, 48 + bob
    grow = p.get("rings", 1.0)
    yy, xx = np.mgrid[0:S, 0:S]

    def ring(rx, ry, tilt, front):
        ca, sa = math.cos(tilt), math.sin(tilt)
        u = (xx + 0.5 - cx) * ca + (yy + 0.5 - cy - 22) * sa
        v = -(xx + 0.5 - cx) * sa + (yy + 0.5 - cy - 22) * ca
        d = (u / rx) ** 2 + (v / ry) ** 2
        band = (d <= 1) & (d >= (1 - 2.4 / ry) ** 2)
        band &= (v > 0) if front else (v <= 0)
        ang = np.arctan2(v / ry, u / rx) - ph
        ticks = band & (np.mod(ang * 6 / math.pi, 1.0) < 0.22)
        return band, ticks

    rings = [(52 * grow, 10 * grow, -0.15), (44 * grow, 16 * grow, 0.45)]
    for rx, ry, t in rings:
        b, tk = ring(rx, ry, t, False)
        cv.fill(b, P["gold"][0])
        cv.fill(tk, P["gold"][1])
    # the hooded void
    hood = cv.ellipse(cx, cy - 10, 20, 22)
    mantle = cv.poly([(cx - 20, cy - 4), (cx + 20, cy - 4), (cx + 34, cy + 56), (cx - 34, cy + 56)])
    body = hood | mantle
    tail = taper(cv, [(cx - 20, cy + 50), (cx - 40, cy + 62 + math.sin(ph) * 3), (cx - 58, cy + 58)], 8)
    body |= tail
    cv.fill(body, P["void"][1])
    cv.fill(body & (noise(S, S, 13, 6.0, 2) > 0.55), P["void"][2])
    rng = np.random.default_rng(5)
    for _ in range(40):
        x, y = rng.integers(0, S), rng.integers(0, S)
        if body[y, x]:
            cv.put(x, y, P["star"][int(rng.integers(0, 2))])
    cv.fill(dilate(body) & ~body, P["gold"][1])
    # stone mask with sun eyes and a stone beard
    mask = cv.ellipse(cx + 3, cy - 8, 10, 12) | cv.poly([(cx - 6, cy - 2), (cx + 13, cy - 2), (cx + 10, cy + 14),
                                                        (cx + 3, cy + 20), (cx - 4, cy + 12)])
    sh(cv, mask, P["stone"], bevel=4, amb=0.2)
    for k in range(4):
        cv.fill(cv.stroke([(cx - 3 + k * 4, cy + 2), (cx - 1 + k * 3, cy + 16 - abs(k - 1.5) * 2)], 1) & mask,
                P["stone"][0])
    eye_c = P["sun"][1] if p.get("flare") else P["sun"][0]
    for ex in (cx + 1, cx + 8):
        cv.fill(cv.ellipse(ex, cy - 8, 2.2, 1.8), eye_c)
        cv.put(ex, cy - 8, P["sun"][1])
    cv.fill(cv.stroke([(cx + 6, cy - 19), (cx + 8, cy - 13), (cx + 6, cy - 10)], 1) & mask, P["sun"][0])
    # hourglass in the chest, sand level by phase
    gx, gy = cx + 1, cy + 30
    glass = cv.poly([(gx - 6, gy - 8), (gx + 6, gy - 8), (gx + 1, gy), (gx + 6, gy + 8), (gx - 6, gy + 8),
                     (gx - 1, gy)])
    cv.fill(glass, P["void"][3])
    lvl = int((ph * 2) % 6)
    cv.fill(glass & (yy > gy + 6 - lvl), P["gold"][2])
    cv.fill(glass & (yy < gy - 2 - lvl) & (yy > gy - 8), P["gold"][1])
    cv.fill(cv.rect(gx, gy, gx, gy + 6), P["gold"][2])
    cv.fill(cv.rect(gx - 7, gy - 10, gx + 7, gy - 9) | cv.rect(gx - 7, gy + 9, gx + 7, gy + 10), P["gold"][1])
    # the harpe: angle from the pose, with a smear arc mid-swing
    ha = math.radians(p.get("harpe", -60))
    hx, hy = cx + 22, cy + 14
    arm = tube(cv, [(cx + 12, cy + 2), (hx, hy)], 4, 3)
    cv.fill(arm, P["void"][2])
    ux, uy = math.cos(ha), math.sin(ha)
    shaft_end = (hx + ux * 14, hy + uy * 14)
    cv.fill(cv.stroke([(hx - ux * 4, hy - uy * 4), shaft_end], 2), P["gold"][1])
    bx, by = shaft_end
    nx, ny = -uy, ux
    blade_pts = bezier((bx, by), (bx + ux * 10 + nx * 10, by + uy * 10 + ny * 10),
                       (bx + nx * 26, by + ny * 26), (bx - ux * 6 + nx * 30, by - uy * 6 + ny * 30), 16)
    blade = taper(cv, blade_pts, 4, ends=(3, 0))
    cv.fill(dilate(blade) & ~blade, P["o"])
    sh(cv, blade, P["stone"], bevel=2, amb=0.3)
    cv.fill(cv.stroke(blade_pts, 1) & blade, P["star"][1])
    if p.get("smear"):
        for k in range(1, 4):
            a2 = ha - k * 0.35
            sm = bezier((hx, hy), (hx + math.cos(a2) * 30, hy + math.sin(a2) * 30),
                        (hx + math.cos(a2 - 0.3) * 40, hy + math.sin(a2 - 0.3) * 40),
                        (hx + math.cos(a2 - 0.6) * 42, hy + math.sin(a2 - 0.6) * 42), 10)
            cv.fill(cv.stroke(sm, 1) & ~cv.opaque(), P["star"][0] if k > 1 else P["star"][1])
    cv.fill(cv.ellipse(hx, hy, 3, 3), P["void"][3])
    # front halves of the rings
    for rx, ry, t in rings:
        b, tk = ring(rx, ry, t, True)
        cv.fill(b, P["gold"][1])
        cv.fill(tk, P["gold"][2])
    cv.outline(P["o"])
    k = p.get("implode", 0)
    if k:
        cv = scale_canvas(cv, 1 - k)
        if k > 0.5:
            cv.fill(cv.ellipse(64, 64, 3 + k * 4, 3 + k * 4) & ~cv.ellipse(64, 64, 2 + k * 4, 2 + k * 4),
                    P["sun"][1])
    return cv


def kronos_poses():
    out = {}
    out["idle"] = [dict(bob=(0, -1, -1, 0)[k], phase=k * 0.4) for k in range(4)]
    out["walk"] = [dict(bob=(0, -1, -2, -2, -1, 0)[k], lean=2, phase=k * 0.6, harpe=-50) for k in range(6)]
    ang = [-120, -140, -40, 20, 50, -60]
    out["attack"] = [dict(harpe=ang[k], smear=k in (2, 3), lean=(-2, -3, 3, 5, 3, 0)[k], phase=k * 0.3)
                     for k in range(6)]
    out["special"] = [dict(rings=1.0 + k * 0.12, flare=True, phase=k * 0.9, harpe=-90) for k in range(6)]
    out["hurt"] = [dict(lean=-5, harpe=-140), dict(lean=-3, harpe=-100)]
    out["death"] = [dict(implode=0.1 + k * 0.15, phase=k * 0.8, flare=True) for k in range(6)]
    return out


GODS = {
    "zeus": (zeus, zeus_poses, ZEUS),
    "poseidon": (poseidon, poseidon_poses, POSEIDON),
    "hades": (hades, hades_poses, HADES),
    "ares": (ares, ares_poses, ARES),
    "kronos": (kronos, kronos_poses, KRONOS),
}


def palette(name) -> list:
    """Transparent + the god's 15 colours, in ramp order (cycle-friendly)."""
    P = GODS[name][2]
    out = [(0, 0, 0)]
    for v in P.values():
        for c in (v if isinstance(v, list) else [v]):
            if tuple(c) not in out:
                out.append(tuple(c))
    assert len(out) <= 16, (name, len(out))
    return out


def frames(name) -> dict:
    fn, poses, _ = GODS[name]
    return {a: [fn(p) for p in seq] for a, seq in poses().items()}


def sheet(name):
    """Vertical strip of 64x64 quadrants, 4 per frame (TL, TR, BL, BR): grit
    -Mw8 -Mh8 then keeps every frame's 4 OAM blocks contiguous."""
    fr = frames(name)
    seq, index = [], {}
    for a in ANIMS:
        index[a] = (len(seq), len(fr[a]))
        seq += fr[a]
    cv = Canvas(64, 64 * 4 * len(seq))
    for i, f in enumerate(seq):
        for q, (qx, qy) in enumerate(((0, 0), (64, 0), (0, 64), (64, 64))):
            cv.paste(f.crop(qx, qy, 64, 64), 0, (i * 4 + q) * 64)
    return cv, index, seq

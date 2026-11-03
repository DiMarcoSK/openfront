"""units.py - the warriors: four civilisations x five classes, fully animated.

Every unit is drawn by one rig (joints per frame) wearing civ-specific gear
bitmaps (helmet, shield, armour, weapon). Gear changes the silhouette, not
just the colours: a Greek line is round shields under crests, a Roman line
is a wall of scuta, Carthage plumes and ovals, Assyria cones and tall
wicker. All frames face right; the left-facing enemy is the OAM h-flip.

Palette contract (one 16-colour OAM palette per civ, index 0 transparent):
the ROLE order below is shared, so a civ swap is a palette swap and the
damage flash is a single palette write.
"""

from __future__ import annotations

import numpy as np

from px import Canvas, ramp, dilate

ROLES = ["clear", "outline", "skin_d", "skin", "skin_l", "metal_d", "metal", "metal_l",
         "cloth_d", "cloth", "cloth_l", "wood_d", "wood", "accent_d", "accent", "shine"]

CIVS = {
    "hellenes": dict(
        name="Hellenes", skin=ramp("#6a3626", "#b0704c", "#e2aa7e"),
        metal=ramp("#5a3014", "#a86a24", "#eab45a"), cloth=ramp("#1a2a5a", "#2c56a0", "#74a0dc"),
        wood=ramp("#3e2414", "#86562e"), accent=ramp("#6e1412", "#d03c2a"),
        outline="#140c0c", shine="#fff4d0"),
    "rome": dict(
        name="Rome", skin=ramp("#62362a", "#ac6e50", "#dca482"),
        metal=ramp("#30323e", "#7a8092", "#cdd2dc"), cloth=ramp("#540c10", "#a81e1e", "#e2564a"),
        wood=ramp("#3a2214", "#7e5030"), accent=ramp("#7a5212", "#e6b43e"),
        outline="#100c10", shine="#ffffff"),
    "carthage": dict(
        name="Carthage", skin=ramp("#4a2618", "#8c5236", "#c48a62"),
        metal=ramp("#5a3a10", "#b48a2e", "#f2d27a"), cloth=ramp("#36103c", "#762478", "#b862b4"),
        wood=ramp("#3a2414", "#7a5432"), accent=ramp("#9e9680", "#f2ecd6"),
        outline="#140a12", shine="#fffbe8"),
    "assyria": dict(
        name="Assyria", skin=ramp("#4e2c1a", "#946040", "#c89468"),
        metal=ramp("#262a2c", "#646c6a", "#acb4ae"), cloth=ramp("#0c3444", "#18768a", "#5cc4c4"),
        wood=ramp("#4a3416", "#a07a3c"), accent=ramp("#7a5414", "#e8c04a"),
        outline="#0c0a0c", shine="#fff8dc"),
}


def palette(civ: str) -> list:
    c = CIVS[civ]
    out = [(0, 0, 0)]
    out.append(ramp(c["outline"])[0])
    out += c["skin"] + c["metal"] + c["cloth"] + c["wood"] + c["accent"]
    out.append(ramp(c["shine"])[0])
    assert len(out) == 16, len(out)
    return out


def keys(civ: str) -> dict:
    p = palette(civ)
    k = dict(zip(ROLES, p))
    return {
        "o": k["outline"], "S": k["skin_d"], "s": k["skin"], "L": k["skin_l"],
        "M": k["metal_d"], "m": k["metal"], "h": k["metal_l"],
        "C": k["cloth_d"], "c": k["cloth"], "l": k["cloth_l"],
        "W": k["wood_d"], "w": k["wood"], "A": k["accent_d"], "a": k["accent"], "*": k["shine"],
    }


# ---------------------------------------------------------------------------
# Gear bitmaps (facing right). Anchor = top-left; '.' is transparent.
# Heads are 7 wide; the face sits on the right (x=4..5) under the brim.
# ---------------------------------------------------------------------------
HEADS = {
    "hellenes": [  # Corinthian helm, fore-aft crest
        ".AAAa..",
        "AAaaaA.",
        ".hhhm..",
        ".hmmmm.",
        ".mmooS.",
        ".Mmmms.",
        "..MM...",
    ],
    "rome": [  # galea: open face, cheek guard, plume knob
        "...a...",
        "..hA...",
        ".hhhm..",
        ".mmmmM.",
        ".MmmsL.",
        ".M.Ms..",
        "...S...",
    ],
    "carthage": [  # Montefortino with twin plumes
        "Al..l..",
        ".A.Al..",
        ".hhhm..",
        ".hmmmm.",
        ".MsSsL.",
        "..Mss..",
        "...S...",
    ],
    "assyria": [  # pointed conical helm, long black beard
        "...h...",
        "..hm...",
        ".hmmM..",
        ".MMMMM.",
        ".oSsoL.",
        ".ooos..",
        "..ooo..",
    ],
}

ARCHER_HEADS = {
    "hellenes": [".......", "..ll...", ".llcc..", ".lccC..", ".SsosL.", ".Sssss.", "..SS..."],
    "rome": [".......", "..hh...", ".hmmm..", ".MmmM..", ".SssoL.", ".Ssss..", "..SS..."],
    "carthage": [".......", "..aa...", ".aaAA..", ".aAAA..", ".SssoL.", ".Ssss..", "..SS..."],
    "assyria": ["...a...", "..aA...", ".aAAA..", ".AAAAA.", ".oSsoL.", ".ooos..", "..oo..."],
}

SHIELDS = {
    "hellenes": [  # aspis: big round, bronze rim, lambda-red field
        ".hhh.",
        "hAaAm",
        "haAAm",
        "hAAAm",
        "mAAAM",
        ".MMM.",
    ],
    "rome": [  # scutum: tall curved rectangle, gold boss
        "hccc",
        "ccCc",
        "caac",
        "ccCc",
        "cccC",
        "cccC",
        "MCCC",
    ],
    "carthage": [  # oval thureos, ivory with purple spine
        ".aa.",
        "aaca",
        "aaca",
        "aaca",
        "aacA",
        ".AA.",
    ],
    "assyria": [  # tall wicker siege shield, rounded top
        ".ww.",
        "wwWw",
        "wWww",
        "wwWw",
        "wWww",
        "wwWw",
        "WWWW",
    ],
}

# Torso (shoulders y=0 .. waist y=3) per civ: armour pattern.
TORSOS = {
    "hellenes": ["hhmm", "hmmM", "mmMM", "cccC"],
    "rome": ["hmhm", "MMMM", "hmhm", "cccC"],
    "carthage": ["lllc", "lccc", "hmmM", "cccC"],
    "assyria": ["mhmM", "MmMm", "mhmM", "cccC"],
}

SKIRT = {"hellenes": "lcCc", "rome": "cCcC", "carthage": "lllc", "assyria": "cccCC"}

WEAPON = {  # spear tip/blade colours and lengths per civ
    "hellenes": dict(spear=11, tip="h"),
    "rome": dict(spear=8, tip="h", sword=True),
    "carthage": dict(spear=12, tip="h"),
    "assyria": dict(spear=10, tip="h"),
}


# ---------------------------------------------------------------------------
# Rig: 16x16 infantry. Joint coordinates are absolute in the 16 px frame.
# ---------------------------------------------------------------------------
def pose(bob=0, lean=0, nk=(9, 12), nf=(10, 14), fk=(7, 12), ff=(6, 14), hand=(10, 8),
         ang=-0.12, shield=(0, 0), thrust=0, drop=False):
    return dict(bob=bob, lean=lean, nk=nk, nf=nf, fk=fk, ff=ff, hand=hand, ang=ang,
                shield=shield, thrust=thrust, drop=drop)


INFANTRY = {
    "idle": [pose(), pose(bob=1, hand=(10, 9))],
    "walk": [
        pose(nk=(10, 12), nf=(11, 14), fk=(6, 12), ff=(5, 14)),
        pose(bob=-1, nk=(9, 12), nf=(8, 14), fk=(8, 12), ff=(8, 14), hand=(10, 7)),
        pose(nk=(7, 12), nf=(6, 14), fk=(9, 12), ff=(11, 14)),
        pose(bob=-1, nk=(8, 12), nf=(8, 14), fk=(9, 12), ff=(9, 14), hand=(10, 7)),
    ],
    "attack": [
        pose(lean=-1, hand=(7, 7), ang=-0.05, nk=(9, 12), nf=(10, 14), fk=(6, 12), ff=(5, 14)),
        pose(lean=1, hand=(12, 8), ang=0.0, thrust=3, shield=(1, 0),
             nk=(11, 12), nf=(12, 14), fk=(6, 12), ff=(4, 14)),
        pose(lean=0, hand=(11, 8), ang=-0.05, thrust=1, nk=(10, 12), nf=(11, 14)),
    ],
    "hurt": [pose(lean=-1, bob=0, hand=(9, 7), ang=-0.6, shield=(-1, -1))],
}


def draw_leg(cv, hip, knee, foot, color, shade):
    cv.line(hip[0], hip[1], knee[0], knee[1], color)
    cv.line(knee[0], knee[1], foot[0], foot[1], color)
    cv.put(foot[0] + 1, foot[1], shade)


def spear_line(cv, hand, ang, length, tip_len, K, back=4):
    import math
    dx, dy = math.cos(ang), math.sin(ang)
    x0, y0 = hand[0] - dx * back, hand[1] - dy * back
    x1, y1 = hand[0] + dx * length, hand[1] + dy * length
    cv.line(x0, y0, x1, y1, K["W"])
    cv.line(x1 - dx * tip_len, y1 - dy * tip_len, x1, y1, K["h"])
    cv.put(x1, y1, K["*"])


def infantry_frame(civ: str, p: dict, cls="spear") -> Canvas:
    K = keys(civ)
    cv = Canvas(16, 16)
    b, ln = p["bob"], p["lean"]
    hip_n, hip_f = (8, 10 + b), (7, 10 + b)
    gear = WEAPON[civ]

    # far leg (shadowed), far arm + weapon behind body
    draw_leg(cv, hip_f, (p["fk"][0], p["fk"][1] + b // 2), p["ff"], K["C"], K["W"])
    hand = (p["hand"][0] + ln, p["hand"][1] + b)
    if cls == "spear":
        hand = (hand[0], hand[1] - 3)
    cv.line(7 + ln, 6 + b, hand[0], hand[1], K["S"])

    # torso + skirt
    tx, ty = 6 + ln, 6 + b
    cv.sprite(TORSOS[civ], K, tx, ty)
    cv.sprite([SKIRT[civ]], K, tx - (1 if len(SKIRT[civ]) > 4 else 0), ty + 4)
    # near leg with greave
    nk = (p["nk"][0], p["nk"][1] + b // 2)
    draw_leg(cv, hip_n, nk, p["nf"], K["s"], K["W"])
    cv.put(nk[0], nk[1] + 1, K["m"])
    cv.put((nk[0] + p["nf"][0]) // 2, nk[1] + 1, K["m"])

    # head + helmet
    cv.sprite(HEADS[civ], K, 5 + ln, -1 + b)

    # shield in front (near arm)
    if not p["drop"]:
        sh = Canvas(16, 16)
        rows = SHIELDS[civ]
        sx = 7 + ln + p["shield"][0] - (1 if len(rows[0]) > 4 else 0)
        sy = 6 + b + p["shield"][1] - (1 if len(rows) > 6 else 0)
        sh.sprite(rows, K, sx, sy)
        sh.outline(K["o"])
        cv.paste(sh, 0, 0)
    if cls == "spear":
        spear_line(cv, hand, p["ang"] + 0.1, gear["spear"] - 3 + p["thrust"], 2, K, back=7)
        cv.put(hand[0], hand[1], K["s"])
    elif cls == "blade":
        import math
        a = p["ang"] - 1.0 + p["thrust"] * 0.4
        hx, hy = hand[0], hand[1] - 1
        cv.line(hx, hy, hx + math.cos(a) * 5, hy + math.sin(a) * 5, K["h"])
        cv.line(hx + 1, hy, hx + 1 + math.cos(a) * 4, hy + math.sin(a) * 4, K["m"])
        cv.put(hx + math.cos(a) * 5, hy + math.sin(a) * 5, K["*"])
        cv.put(hx, hy, K["a"])
        cv.put(hx - 1, hy + 1, K["s"])
    cv.outline(K["o"])
    return cv


def death_frames(civ: str, cls: str, hurt: Canvas) -> list:
    """Stagger -> kneel -> corpse. The corpse is the hurt frame rotated 90."""
    K = keys(civ)
    stagger = infantry_frame(civ, pose(lean=-1, bob=1, hand=(8, 9), ang=-1.0, shield=(-1, 1),
                                       nk=(8, 12), nf=(9, 14), fk=(6, 12), ff=(4, 14)), cls)
    kneel = Canvas(16, 16)
    kneel.paste(infantry_frame(civ, pose(bob=3, lean=-1, hand=(7, 9), ang=0.5, drop=True,
                                         nk=(10, 13), nf=(10, 14), fk=(6, 13), ff=(4, 14)), cls), 0, 0)
    corpse = Canvas(16, 16)
    rot = np.rot90(hurt.a, k=1).copy()
    tmp = Canvas(16, 16)
    tmp.a[:] = rot
    ys = np.nonzero(tmp.opaque())[0]
    corpse.paste(tmp, 0, 15 - ys.max())
    return [stagger, kneel, corpse]


ARCHER = {
    "idle": [dict(bob=0, draw=0), dict(bob=1, draw=0)],
    "walk": [dict(bob=0, draw=0, step=0), dict(bob=-1, draw=0, step=1),
             dict(bob=0, draw=0, step=2), dict(bob=-1, draw=0, step=3)],
    "attack": [dict(bob=0, draw=1), dict(bob=0, draw=2), dict(bob=0, draw=3)],
    "hurt": [dict(bob=0, draw=0, hurt=True)],
}

STEPS = [((10, 12), (11, 14), (6, 12), (5, 14)), ((9, 12), (8, 14), (8, 12), (8, 14)),
         ((7, 12), (6, 14), (9, 12), (11, 14)), ((8, 12), (8, 14), (9, 12), (9, 14))]


def archer_frame(civ: str, p: dict) -> Canvas:
    K = keys(civ)
    cv = Canvas(16, 16)
    b = p["bob"]
    ln = -1 if p.get("hurt") else 0
    nk, nf, fk, ff = STEPS[p.get("step", 0)]
    draw_leg(cv, (7, 10 + b), fk, ff, K["C"], K["W"])
    # quiver on the back
    cv.sprite(["wa", "ww", "Ww", "Ww"], K, 4 + ln, 5 + b)
    cv.sprite(TORSOS[civ][:1] + ["cccc", "cccC", "cccC"] if civ != "rome" else
              ["hmhm", "cccc", "cccC", "cccC"], K, 6 + ln, 6 + b)
    cv.sprite([SKIRT[civ][:4]], K, 6 + ln, 10 + b)
    draw_leg(cv, (8, 10 + b), nk, nf, K["s"], K["W"])
    cv.sprite(ARCHER_HEADS[civ], K, 5 + ln, -1 + b)
    d = p["draw"]
    bx = 12 + ln - (1 if d == 0 else 0)
    # bow: an arc in the near hand, string pulled by the far hand
    bow = [(bx - 1, 4 + b), (bx, 5 + b), (bx + 1, 6 + b), (bx + 1, 7 + b), (bx + 1, 8 + b),
           (bx, 9 + b), (bx - 1, 10 + b)]
    for x, y in bow:
        cv.put(x, y, K["w"])
    cv.put(bx - 1, 4 + b, K["W"])
    cv.put(bx - 1, 10 + b, K["W"])
    pull = {0: 0, 1: 2, 2: 3, 3: 0}[d]
    sx = bx - 1 - pull
    cv.line(bx - 1, 4 + b, sx, 7 + b, K["*"] if d else K["l"])
    cv.line(sx, 7 + b, bx - 1, 10 + b, K["*"] if d else K["l"])
    cv.line(9 + ln, 7 + b, bx + 1, 7 + b, K["s"])  # bow arm
    cv.line(8 + ln, 7 + b, sx, 7 + b, K["S"])      # draw arm
    if d in (1, 2):
        cv.line(sx, 7 + b, bx + 3, 7 + b, K["w"])  # nocked arrow
        cv.put(bx + 3, 7 + b, K["h"])
    if d == 3:
        cv.put(15, 6 + b, K["h"])
        cv.put(14, 6 + b, K["w"])
    cv.outline(K["o"])
    return cv


# ---------------------------------------------------------------------------
# Cavalry (32x32): horse rig + rider. Legs gallop through 4 contact poses.
# ---------------------------------------------------------------------------
HORSE_COAT = {"hellenes": ("w", "W", "o"), "rome": ("s", "S", "o"),
              "carthage": ("l", "c", "C"), "assyria": ("w", "W", "o")}

GALLOP = [  # (front_near, front_far, back_near, back_far) hoof offsets (dx, dy-lift)
    ((6, 0), (3, 0), (-2, 0), (-5, 0)),
    ((4, -3), (7, -1), (-6, 0), (-3, -2)),
    ((1, 0), (5, -2), (-1, -3), (-6, 0)),
    ((7, -1), (2, -3), (-4, -2), (-1, 0)),
]


def horse(cv, K, coat, frame, bob, rear=0):
    c_mid, c_dark, mane = K[coat[0]], K[coat[1]], K[coat[2]]
    body = cv.ellipse(15, 19 + bob, 8.5, 4.6)
    neck = cv.poly([(19, 17 + bob), (23, 11 + bob - rear), (26, 11 + bob - rear), (25, 17 + bob),
                    (22, 20 + bob)])
    head = cv.poly([(23, 9 + bob - rear), (27, 9 + bob - rear), (30, 13 + bob - rear),
                    (29, 15 + bob - rear), (26, 13 + bob - rear), (23, 12 + bob - rear)])
    fr = GALLOP[frame]
    # far legs first (dark)
    for (dx, lift), hipx in ((fr[1], 21), (fr[3], 9)):
        kx = hipx + dx // 2
        cv.line(hipx, 21 + bob, kx, 25 + bob + lift // 2, c_dark)
        cv.line(kx, 25 + bob + lift // 2, hipx + dx, 29 + lift, c_dark)
        cv.put(hipx + dx, 29 + lift, K["o"])
    cv.fill(body | neck | head, c_mid)
    cv.shade(body, [c_dark, c_mid, c_mid], light=(-0.3, -0.9, 0.5), bevel=3, ambient=0.3)
    cv.fill(cv.rect(9, 22 + bob, 22, 23 + bob) & body, c_dark)
    for (dx, lift), hipx in ((fr[0], 21), (fr[2], 10)):
        kx = hipx + dx // 2
        cv.line(hipx, 21 + bob, kx, 25 + bob + lift // 2, c_mid)
        cv.line(hipx + 1, 21 + bob, kx + 1, 25 + bob + lift // 2, c_mid)
        cv.line(kx, 25 + bob + lift // 2, hipx + dx, 29 + lift, c_mid)
        cv.put(hipx + dx, 29 + lift, K["o"])
        cv.put(hipx + dx + 1, 29 + lift, K["o"])
    # mane, tail, eye, bridle
    cv.line(20, 15 + bob, 23, 10 + bob - rear, mane)
    cv.line(21, 15 + bob, 24, 10 + bob - rear, mane)
    tail = [(7, 17 + bob), (5, 18 + bob), (4, 20 + bob + (frame % 2)), (3, 23 + bob + (frame % 2))]
    for i in range(len(tail) - 1):
        cv.line(*tail[i], *tail[i + 1], mane)
    cv.put(26, 11 + bob - rear, K["o"])
    cv.line(27, 13 + bob - rear, 24, 15 + bob - rear, K["a"])
    return body


def rider(cv, K, civ, bob, lance, thrust=0):
    hx, hy = 13, 4 + bob
    cv.sprite(HEADS[civ], K, hx, hy)
    cv.sprite(TORSOS[civ], K, hx + 1, hy + 7)
    cv.sprite(["cc", "cC", "CC"], K, hx + 2, hy + 11)  # thigh over the barrel
    cv.line(hx + 3, hy + 13, hx + 2, hy + 16, K["s"])
    import math
    a, ln = lance
    gx, gy = hx + 5 + thrust, hy + 9
    dx, dy = math.cos(a), math.sin(a)
    cv.line(gx - dx * 6, gy - dy * 6, gx + dx * ln, gy + dy * ln, K["W"])
    cv.line(gx + dx * (ln - 2), gy + dy * (ln - 2), gx + dx * ln, gy + dy * ln, K["h"])
    cv.line(hx + 3, hy + 8, gx, gy, K["s"])
    # small round shield on the near side
    sh = Canvas(32, 32)
    sh.sprite([".mm.", "mAam", "mAAm", ".MM."], K, hx - 1, hy + 9)
    sh.outline(K["o"])
    cv.paste(sh, 0, 0)


def cavalry_frame(civ, anim, i) -> Canvas:
    K = keys(civ)
    cv = Canvas(32, 32)
    coat = HORSE_COAT[civ]
    if anim == "idle":
        bob, frame, lance, thrust, rear = (0 if i == 0 else 1), 0, (-1.2, 12), 0, 0
    elif anim == "walk":
        bob, frame, lance, thrust, rear = (0, -1, 0, -1)[i], i, (-0.9, 12), 0, 0
    elif anim == "attack":
        bob, frame, lance, thrust, rear = 0, (1, 3, 0)[i], ((-1.0, 12), (0.05, 13), (-0.3, 12))[i], \
            (0, 3, 1)[i], (2, 0, 0)[i]
    else:  # hurt
        bob, frame, lance, thrust, rear = 1, 0, (-1.6, 12), -1, 3
    tmp = Canvas(32, 32)
    horse(tmp, K, coat, frame, bob, rear)
    rider(tmp, K, civ, bob, lance, thrust)
    tmp.outline(K["o"])
    cv.paste(tmp, 0, 0)
    return cv


# ---------------------------------------------------------------------------
# General (32x32): the player's avatar rides; cape, tall plume, raised blade.
# ---------------------------------------------------------------------------
def general_frame(civ, anim, i) -> Canvas:
    import math
    K = keys(civ)
    tmp = Canvas(32, 32)
    bob = {"idle": (0, 1)[i % 2], "walk": (0, -1, 0, -1)[i % 4], "attack": 0, "command": 0,
           "hurt": 1}[anim]
    frame = {"walk": i % 4, "attack": (1, 3, 0)[i % 3]}.get(anim, 0)
    rear = {"command": (2, 3)[i % 2], "hurt": 3, "attack": (2, 0, 0)[i % 3]}.get(anim, 0)
    # cape streaming behind, longer when moving
    flow = 3 if anim in ("walk", "attack") else 0
    cape = tmp.poly([(15, 9 + bob), (17, 10 + bob), (14, 20 + bob), (5 - flow, 18 + bob + (i % 2)),
                     (9 - flow, 13 + bob)])
    tmp.shade(cape, [K["A"], K["a"]], light=(-0.4, -0.8, 0.5), bevel=2, ambient=0.3, dither=0.3)
    horse(tmp, K, ("C", "o", "a") if civ != "carthage" else ("l", "c", "a"), frame, bob, rear)
    hx, hy = 13, 4 + bob
    tmp.sprite(HEADS[civ], K, hx, hy)
    tmp.sprite(["haah", "hmmM", "mhmM", "aaaa"], K, hx + 1, hy + 7)
    tmp.sprite(["cc", "cC", "CC"], K, hx + 2, hy + 11)
    tmp.line(hx + 3, hy + 13, hx + 2, hy + 16, K["s"])
    # commander plume: tall, sweeping back
    tmp.fill(tmp.poly([(hx + 1, hy - 1), (hx + 5, hy - 4), (hx + 3, hy + 1)]), K["a"])
    tmp.fill(tmp.poly([(hx - 3, hy + 1), (hx + 2, hy - 3), (hx + 2, hy)]), K["A"])
    # sword arm
    ang = {"idle": -0.6, "walk": -0.5, "hurt": -2.2,
           "attack": (-2.0, 0.1, -0.3)[i % 3], "command": (-1.45, -1.57)[i % 2]}[anim]
    sx, sy = hx + 6, hy + 8
    ex, ey = sx + math.cos(ang) * 3, sy + math.sin(ang) * 3
    tmp.line(hx + 3, hy + 8, ex, ey, K["s"])
    tx, ty = ex + math.cos(ang) * 8, ey + math.sin(ang) * 8
    tmp.line(ex, ey, tx, ty, K["h"])
    tmp.line(ex + 1, ey, tx + 1, ty, K["m"])
    tmp.put(tx, ty, K["*"])
    tmp.put(ex, ey, K["a"])
    tmp.outline(K["o"])
    if anim == "command":
        for k in range(4):
            a = k * math.pi / 2 + i * 0.4
            tmp.put(tx + math.cos(a) * 2, ty + math.sin(a) * 2, K["*"])
    return tmp


def horse_down(civ, coat_keys, rider_civ=None) -> Canvas:
    """A fallen horse on its side, legs out, rider thrown in front."""
    K = keys(civ)
    cv = Canvas(32, 32)
    c_mid, c_dark, mane = K[coat_keys[0]], K[coat_keys[1]], K[coat_keys[2]]
    for x0, x1 in ((8, 5), (11, 10), (19, 22), (22, 26)):
        cv.line(x0, 27, x1, 23, c_dark)
        cv.put(x1, 22, K["o"])
    body = cv.ellipse(15, 27.5, 9, 3.6)
    cv.shade(body, [c_dark, c_mid, c_mid], light=(-0.3, -0.9, 0.5), bevel=2, ambient=0.3)
    head = cv.poly([(23, 26), (29, 27), (30, 30), (24, 30)])
    cv.fill(head, c_mid)
    cv.line(23, 25, 27, 26, mane)
    cv.line(6, 28, 3, 30, mane)
    cv.put(27, 28, K["o"])
    cv.outline(K["o"])
    return cv


# ---------------------------------------------------------------------------
# Sheet assembly
# ---------------------------------------------------------------------------
ANIM_ORDER = ["idle", "walk", "attack", "hurt", "death"]


def unit_frames(civ: str, cls: str) -> dict:
    frames = {}
    if cls in ("spear", "blade"):
        for a in ("idle", "walk", "attack", "hurt"):
            frames[a] = [infantry_frame(civ, p, cls) for p in INFANTRY[a]]
        frames["death"] = death_frames(civ, cls, frames["hurt"][0])
    elif cls == "archer":
        for a in ("idle", "walk", "attack", "hurt"):
            frames[a] = [archer_frame(civ, p) for p in ARCHER[a]]
        hurt = frames["hurt"][0]
        corpse = Canvas(16, 16)
        tmp = Canvas(16, 16)
        tmp.a[:] = np.rot90(hurt.a, k=1)
        ys = np.nonzero(tmp.opaque())[0]
        corpse.paste(tmp, 0, 15 - ys.max())
        kneel = Canvas(16, 16)
        kneel.paste(hurt, 0, 2)
        kneel.erase(kneel.rect(0, 15, 15, 15))
        frames["death"] = [hurt, kneel, corpse]
    elif cls == "cavalry":
        counts = dict(idle=2, walk=4, attack=3, hurt=1)
        for a, n in counts.items():
            frames[a] = [cavalry_frame(civ, a, i) for i in range(n)]
        hurt = frames["hurt"][0]
        fall = Canvas(32, 32)
        fall.paste(hurt, 0, 3)
        frames["death"] = [hurt, fall, horse_down(civ, HORSE_COAT[civ])]
    elif cls == "general":
        counts = dict(idle=2, walk=4, attack=3, command=2, hurt=1)
        for a, n in counts.items():
            frames[a] = [general_frame(civ, a, i) for i in range(n)]
        hurt = frames["hurt"][0]
        fall = Canvas(32, 32)
        fall.paste(hurt, 0, 3)
        coat = ("C", "o", "a") if civ != "carthage" else ("l", "c", "a")
        frames["death"] = [hurt, fall, horse_down(civ, coat)]
    return frames


CLASSES = ["spear", "blade", "archer", "cavalry", "general"]
FRAME_SIZE = {"spear": 16, "blade": 16, "archer": 16, "cavalry": 32, "general": 32}


def sheet(civ: str, cls: str) -> tuple:
    """One strip per unit: all frames left-to-right, anim order fixed."""
    frames = unit_frames(civ, cls)
    order = [a for a in ("idle", "walk", "attack", "command", "hurt", "death") if a in frames]
    seq = [f for a in order for f in frames[a]]
    s = FRAME_SIZE[cls]
    cv = Canvas(s * len(seq), s)
    for i, f in enumerate(seq):
        cv.paste(f, i * s, 0)
    index = {}
    k = 0
    for a in order:
        index[a] = (k, len(frames[a]))
        k += len(frames[a])
    return cv, index


if __name__ == "__main__":
    import sys
    from pathlib import Path
    out = Path(sys.argv[1] if len(sys.argv) > 1 else ".")
    civs = sys.argv[2:] or list(CIVS)
    for civ in civs:
        rows = [sheet(civ, c)[0] for c in CLASSES]
        w = max(r.w for r in rows)
        h = sum(r.h + 4 for r in rows)
        board = Canvas(w, h, fill=(40, 44, 36))
        y = 0
        for r in rows:
            board.paste(r, 0, y)
            y += r.h + 4
        board.save(out / f"units_{civ}.png", 6)
        print(civ, board.w, board.h)

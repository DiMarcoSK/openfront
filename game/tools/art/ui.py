"""ui.py - the War Table interface kit.

The menus are the general's table: parchment order-sheets and ink (the
BRANDBOOK tokens, unchanged) held down by dark bronze and iron fittings,
the brandbook's two support colours. Bronze is what ties the ledger to the
mythic field: the same metal as the hoplite shields and the boss frames.

Three 16-colour palettes:
  UI     - panels, buttons, bars, pins (BG 4bpp, both screens)
  ICON   - resource / command icons (OAM 16x16)
  RELIC  - the five divine relics won from the gods (OAM 16x16, 32x32)
"""

import math

import numpy as np

from px import Canvas, ramp, hexc, dilate
import font

T = dict(
    ink=hexc("#1E241C"), ink2=hexc("#4F5445"), line=hexc("#C9BFA4"), paper=hexc("#EDE6D0"),
    surface=hexc("#F6F1E3"), accent=hexc("#2E5A27"), accent_l=hexc("#4E8A3E"),
    warn=hexc("#7A4A00"), warn_l=hexc("#C8862A"), crit=hexc("#8C1B15"), crit_l=hexc("#D0442E"),
    bronze_d=hexc("#5E4214"), bronze=hexc("#C89A3C"), bronze_l=hexc("#F0D27A"),
    iron=hexc("#2B2F33"),
)
UI_PAL = [(0, 0, 0), T["ink"], T["ink2"], T["line"], T["paper"], T["surface"], T["accent"],
          T["accent_l"], T["warn"], T["warn_l"], T["crit"], T["crit_l"], T["bronze_d"],
          T["bronze"], T["bronze_l"], T["iron"]]

ICON_PAL = [(0, 0, 0)] + ramp("#1a1410", "#4F5445", "#C9BFA4", "#F6F1E3", "#2E5A27", "#6aa84e",
                               "#7A4A00", "#e0a030", "#8C1B15", "#D0442E", "#5E4214", "#C89A3C",
                               "#F0D27A", "#3a6ea0", "#8ac8f0")
RELIC_PAL = [(0, 0, 0)] + ramp("#0c0a10", "#2b2f33", "#6a7480", "#c8d0dc", "#ffffff", "#2f6bd8",
                                "#6fd8ff", "#1e5856", "#3aae86", "#7ef0b8", "#6a1406", "#f47c1c",
                                "#ffc04a", "#5e4214", "#c89a3c")
I = dict(zip(["o", "g", "l", "s", "a", "A", "w", "W", "c", "C", "bd", "b", "bl", "u", "U"],
             ICON_PAL[1:]))
R = dict(zip(["o", "i", "st", "sl", "wh", "bd", "bc", "td", "tg", "tl", "fd", "f", "fl", "gd", "g"],
             RELIC_PAL[1:]))


# ---------------------------------------------------------------------------
# Panels: 9-slice in 8x8 tiles. Corner = bronze bracket with a rivet.
# ---------------------------------------------------------------------------
def panel(w, h, kind="sheet") -> Canvas:
    """kind: sheet (parchment order sheet), iron (dark command plate),
    boss (crimson monumental frame)."""
    cv = Canvas(w, h)
    fill = {"sheet": T["paper"], "iron": T["iron"], "boss": T["crit"]}[kind]
    inner = {"sheet": T["surface"], "iron": T["ink"], "boss": T["iron"]}[kind]
    cv.fill(cv.rect(0, 0, w - 1, h - 1), T["ink"])
    cv.fill(cv.rect(1, 1, w - 2, h - 2), fill)
    cv.fill(cv.rect(3, 3, w - 4, h - 4), inner)
    if kind == "sheet":
        # oilcloth seam: a dashed line inset 2 px from the edge
        for x in range(4, w - 4, 4):
            cv.put(x, 2, T["line"])
            cv.put(x, h - 3, T["line"])
        for y in range(4, h - 4, 4):
            cv.put(2, y, T["line"])
            cv.put(w - 3, y, T["line"])
    else:
        cv.fill(cv.rect(1, 1, w - 2, 1), T["bronze_d"] if kind == "iron" else T["crit_l"])
        cv.fill(cv.rect(2, h - 3, w - 3, h - 3), T["ink"])
    # bronze corner brackets with a rivet
    for cx, cy, sx, sy in ((0, 0, 1, 1), (w - 1, 0, -1, 1), (0, h - 1, 1, -1), (w - 1, h - 1, -1, -1)):
        for i in range(7):
            cv.put(cx + sx * i, cy, T["bronze_d"])
            cv.put(cx, cy + sy * i, T["bronze_d"])
        for i in range(1, 6):
            cv.put(cx + sx * i, cy + sy, T["bronze"])
            cv.put(cx + sx, cy + sy * i, T["bronze"])
        cv.put(cx + sx, cy + sy, T["bronze_l"])
        cv.put(cx + sx * 3, cy + sy * 3, T["bronze_d"])
        cv.put(cx + sx * 3 - (1 if sx > 0 else -1), cy + sy * 3 - (1 if sy > 0 else -1), T["bronze_l"])
    return cv


def button(w, h, label, state="normal") -> Canvas:
    """normal / focus / pressed / disabled. Text on accent is on-accent only."""
    cv = Canvas(w, h)
    if state == "disabled":
        cv.fill(cv.rect(0, 0, w - 1, h - 1), T["paper"])
        for x in range(0, w, 2):
            cv.put(x, 0, T["ink2"])
            cv.put(x, h - 1, T["ink2"])
        for y in range(0, h, 2):
            cv.put(0, y, T["ink2"])
            cv.put(w - 1, y, T["ink2"])
        ink, sh = T["ink2"], None
    else:
        base = {"normal": T["surface"], "focus": T["accent"], "pressed": T["ink"]}[state]
        cv.fill(cv.rect(0, 0, w - 1, h - 1), T["ink"])
        cv.fill(cv.rect(1, 1, w - 2, h - 3 if state != "pressed" else h - 2), base)
        if state == "focus":
            cv.fill(cv.rect(1, 1, w - 2, 1), T["accent_l"])
            cv.fill(cv.rect(1, h - 2, w - 2, h - 2), T["ink"])
            for x in (1, w - 2):
                cv.put(x, 1, T["bronze_l"])
            # bronze chevrons point at the focused action
            for k in range(3):
                cv.put(3 + k, h // 2 - 2 + k, T["bronze_l"])
                cv.put(3 + k, h // 2 + 2 - k, T["bronze_l"])
        elif state == "normal":
            cv.fill(cv.rect(1, h - 2, w - 2, h - 2), T["line"])
        ink = {"normal": T["ink"], "focus": T["surface"], "pressed": T["bronze_l"]}[state]
        sh = T["ink"] if state == "focus" else None
    tx = (w - font.width(label)) // 2
    ty = (h - 7) // 2 - (0 if state == "pressed" else 1) + (1 if state == "pressed" else 0)
    font.text(cv, tx, ty, label, ink, sh)
    return cv


def bar(w, value, kind="hp") -> Canvas:
    """8 px meter: ink trough, enamel fill, bronze end caps, tick every 25%."""
    cv = Canvas(w, 8)
    col, hi = {"hp": (T["accent"], T["accent_l"]), "morale": (T["warn"], T["warn_l"]),
               "boss": (T["crit"], T["crit_l"])}[kind]
    cv.fill(cv.rect(0, 1, w - 1, 6), T["ink"])
    cv.fill(cv.rect(2, 2, w - 3, 5), T["iron"])
    fw = int((w - 4) * value)
    if fw > 0:
        cv.fill(cv.rect(2, 2, 1 + fw, 5), col)
        cv.fill(cv.rect(2, 2, 1 + fw, 2), hi)
    for k in (1, 2, 3):
        cv.put(2 + (w - 4) * k // 4, 5, T["ink"])
    for x in (0, w - 1):
        cv.fill(cv.rect(x, 0, x, 7), T["bronze"])
        cv.put(x, 0, T["bronze_l"])
    return cv


def pin(color, glyph) -> Canvas:
    """16 px enamel threat pin (BRANDBOOK icons): engraved outline + glint."""
    cv = Canvas(16, 16)
    body = cv.ellipse(8, 7, 6, 6)
    tip = cv.poly([(5, 11), (11, 11), (8, 15)])
    cv.fill(dilate(body | tip) & ~(body | tip), T["ink"])
    cv.fill(body | tip, color)
    cv.fill(cv.ellipse(8, 7, 4, 4) & ~cv.ellipse(9, 8, 4, 4), T["surface"])
    cv.fill(cv.ellipse(8, 7, 4, 4) & cv.ellipse(9, 8, 4, 4), color)
    for x, y in glyph:
        cv.put(x, y, T["surface"])
    cv.put(5, 4, T["surface"])
    return cv


PIN_GLYPHS = {
    "low": [(6, 7), (7, 7), (8, 7), (9, 7), (10, 7)],
    "medium": [(6, 6), (7, 6), (8, 6), (9, 6), (10, 6), (6, 8), (7, 8), (8, 8), (9, 8), (10, 8)],
    "high": [(8, 4), (7, 5), (9, 5), (6, 6), (10, 6), (6, 7), (7, 7), (8, 7), (9, 7), (10, 7)],
    "critical": [(8, 3), (8, 4), (8, 5), (8, 6), (8, 7), (8, 9)],
}


def pins() -> dict:
    return {
        "threat_low": pin(T["accent"], PIN_GLYPHS["low"]),
        "threat_medium": pin(T["warn"], PIN_GLYPHS["medium"]),
        "threat_high": pin(T["ink2"], PIN_GLYPHS["high"]),
        "threat_critical": pin(T["crit"], PIN_GLYPHS["critical"]),
    }


# ---------------------------------------------------------------------------
# Icons (16x16, ICON palette): resources and commands.
# ---------------------------------------------------------------------------
def _ico():
    return Canvas(16, 16)


def ico_gold():
    cv = _ico()
    for k, (x, y) in enumerate(((4, 11), (8, 12), (6, 8), (10, 9), (8, 5))):
        c = cv.ellipse(x + 0.5, y + 0.5, 3.5, 2.2)
        cv.fill(c, I["b"])
        cv.fill(c & ~np.roll(c, -1, axis=0), I["bd"])
        cv.fill(c & ~np.roll(c, 1, axis=0), I["bl"])
    cv.put(9, 5, I["a"])
    cv.outline(I["o"])
    return cv


def ico_food():  # wheat sheaf bound with a cord
    cv = _ico()
    for dx in (-3, -1, 1, 3):
        cv.line(8 + dx // 2, 14, 8 + dx, 5, I["W"])
        for k in range(3):
            cv.put(8 + dx - (1 if dx < 0 else 0), 3 + k * 2, I["bl"])
            cv.put(8 + dx + (1 if dx > 0 else 0), 4 + k * 2, I["b"])
    cv.fill(cv.rect(6, 9, 10, 10), I["c"])
    cv.outline(I["o"])
    return cv


def ico_materials():  # timber stacked on cut stone
    cv = _ico()
    cv.fill(cv.rect(2, 10, 13, 14), I["l"])
    cv.fill(cv.rect(2, 10, 13, 10), I["s"])
    cv.line(8, 11, 8, 14, I["g"])
    for y in (5, 7):
        cv.fill(cv.rect(3, y, 12, y + 1), I["w"])
        cv.fill(cv.rect(3, y, 12, y), I["W"])
        cv.put(12, y, I["bl"])
        cv.put(12, y + 1, I["b"])
    cv.outline(I["o"])
    return cv


def ico_knowledge():  # scroll, half unrolled, with ink lines
    cv = _ico()
    cv.fill(cv.rect(3, 4, 12, 12), I["s"])
    for y in (6, 8, 10):
        cv.line(5, y, 10 - (y == 10) * 2, y, I["g"])
    for y in (3, 13):
        cv.fill(cv.rect(2, y, 13, y + 1), I["b"])
        cv.put(2, y, I["bd"])
        cv.put(13, y, I["bl"])
    cv.outline(I["o"])
    return cv


def ico_manpower():  # crested helm
    cv = _ico()
    helm = cv.ellipse(8, 9, 5, 5) | cv.rect(3, 9, 12, 13)
    cv.fill(helm, I["b"])
    cv.shade(helm, [I["bd"], I["b"], I["bl"]], light=(-0.6, -0.7, 0.5), bevel=2, ambient=0.3)
    cv.fill(cv.rect(5, 9, 11, 10) & helm, I["o"])
    cv.fill(cv.rect(7, 10, 8, 13), I["o"])
    cv.fill(cv.poly([(3, 4), (8, 1), (13, 3), (12, 5), (8, 4), (4, 6)]), I["c"])
    cv.put(8, 2, I["C"])
    cv.outline(I["o"])
    return cv


def ico_morale():  # laurel wreath
    cv = _ico()
    for k in range(6):
        a = math.pi * (0.62 + k * 0.17)
        for side in (-1, 1):
            x = 8 + side * math.cos(a) * -5.5
            y = 8 + math.sin(a) * 5.5
            cv.fill(cv.ellipse(x, y, 1.6, 1.0), I["A"] if k % 2 else I["a"])
    cv.put(8, 13, I["b"])
    cv.put(7, 14, I["b"])
    cv.put(9, 14, I["b"])
    cv.outline(I["o"])
    return cv


def ico_attack():  # crossed swords
    cv = _ico()
    for s in (1, -1):
        x0, x1 = (3, 12) if s == 1 else (12, 3)
        cv.line(x0, 3, x1, 12, I["s"])
        cv.line(x0 + s, 3, x1, 11, I["l"])
        cv.line(x1 - s * 2, 10, x1 - s * 0, 12, I["b"])
        cv.put(x1 + s, 13, I["bd"])
    cv.outline(I["o"])
    return cv


def ico_defense():  # round shield with bronze rim
    cv = _ico()
    s = cv.ellipse(8, 8, 6, 6)
    cv.fill(s, I["b"])
    cv.shade(cv.ellipse(8, 8, 4.5, 4.5), [I["C"], I["c"]], light=(-0.6, -0.7, 0.5), bevel=2,
             ambient=0.3)
    cv.fill(cv.ellipse(8, 8, 1.5, 1.5), I["bl"])
    cv.fill(s & ~np.roll(s, 1, axis=0) & ~np.roll(s, 1, axis=1), I["bl"])
    cv.outline(I["o"])
    return cv


def ico_speed(n):  # chevrons = speed multiplier; pause when n == 0
    cv = _ico()
    if n == 0:
        cv.fill(cv.rect(4, 3, 6, 12), I["s"])
        cv.fill(cv.rect(9, 3, 11, 12), I["s"])
    else:
        cs = [(8,), (5, 10), (3, 7, 11)][{1: 0, 2: 1, 4: 2}[n]]
        for x in cs:
            for k in range(4):
                cv.put(x - 2 + k, 4 + k, I["bl"])
                cv.put(x - 2 + k, 11 - k, I["bl"])
                cv.put(x - 3 + k, 4 + k, I["b"])
                cv.put(x - 3 + k, 11 - k, I["b"])
    cv.outline(I["o"])
    return cv


def ico_casualties():  # broken spear over a skull
    cv = _ico()
    sk = cv.ellipse(8, 7, 4.5, 4.5) | cv.rect(6, 9, 10, 12)
    cv.fill(sk, I["s"])
    cv.fill(cv.ellipse(6, 7, 1.2, 1.4) | cv.ellipse(10, 7, 1.2, 1.4), I["o"])
    cv.put(8, 9, I["o"])
    for x in (7, 9):
        cv.put(x, 12, I["g"])
    cv.line(1, 14, 5, 10, I["W"])
    cv.line(11, 6, 14, 2, I["W"])
    cv.put(14, 1, I["l"])
    cv.outline(I["o"])
    return cv


def ico_tier():  # an ionic column, the city tier mark
    cv = _ico()
    cv.fill(cv.rect(5, 4, 10, 12), I["s"])
    for x in (6, 8, 10):
        cv.line(x, 5, x, 11, I["l"])
    cv.fill(cv.rect(3, 2, 12, 3), I["b"])
    cv.put(3, 3, I["bl"])
    cv.put(12, 3, I["bl"])
    cv.fill(cv.rect(3, 13, 12, 14), I["b"])
    cv.outline(I["o"])
    return cv


def ico_battle():  # banner on a spear, the 'march to the front' action
    cv = _ico()
    cv.line(4, 1, 4, 14, I["W"])
    cv.put(4, 0, I["l"])
    flag = cv.poly([(5, 2), (13, 3), (11, 6), (13, 9), (5, 9)])
    cv.fill(flag, I["c"])
    cv.fill(flag & ~np.roll(flag, -1, axis=0), I["C"])
    cv.fill(cv.ellipse(8, 5.5, 1.6, 1.6), I["bl"])
    cv.outline(I["o"])
    return cv


ICONS = dict(gold=ico_gold, food=ico_food, materials=ico_materials, knowledge=ico_knowledge,
             manpower=ico_manpower, morale=ico_morale, attack=ico_attack, defense=ico_defense,
             speed1=lambda: ico_speed(1), speed2=lambda: ico_speed(2), speed4=lambda: ico_speed(4),
             pause=lambda: ico_speed(0), casualties=ico_casualties, tier=ico_tier, battle=ico_battle)


# ---------------------------------------------------------------------------
# Divine relics (RELIC palette): the permanent reward from each god.
# ---------------------------------------------------------------------------
def relic_keraunos(s=16):
    cv = Canvas(s, s)
    k = s / 16
    pts = [(3, 15), (7, 9), (5, 8), (10, 4), (8, 3), (13, 0)]
    bolt = cv.poly([(x * k, y * k) for x, y in [(2, 15), (6, 8), (4, 8), (10, 2), (12, 0), (9, 5),
                                                 (11, 5), (4, 15)]])
    cv.fill(dilate(bolt) & ~bolt, R["bc"])
    cv.fill(bolt, R["wh"])
    cv.fill(cv.stroke([(x * k, y * k) for x, y in pts], max(1, int(k))) & bolt, R["bc"])
    cv.outline(R["o"])
    return cv


def relic_trident(s=16):
    cv = Canvas(s, s)
    k = s / 16
    cv.fill(cv.rect(int(7 * k), int(5 * k), int(8 * k) - (k < 2), int(15 * k)), R["gd"])
    cv.fill(cv.rect(int(4 * k), int(5 * k), int(11 * k), int(6 * k)), R["g"])
    for x in (4, 7.5, 11):
        cv.fill(cv.poly([((x - 1) * k, 6 * k), ((x + 1) * k, 6 * k), (x * k, 0.5 * k)]), R["tl"])
        cv.put(x * k, 2 * k, R["wh"])
    cv.outline(R["o"])
    return cv


def relic_helm(s=16):  # the Helm of Darkness: Hades' invisibility cap
    cv = Canvas(s, s)
    k = s / 16
    helm = cv.ellipse(8 * k, 8 * k, 6 * k, 6 * k) | cv.rect(int(2 * k), int(8 * k), int(13 * k), int(13 * k))
    cv.fill(helm, R["i"])
    cv.shade(helm, [R["o"], R["i"], R["st"]], light=(-0.5, -0.8, 0.5), bevel=2 * k, ambient=0.2)
    cv.fill(cv.rect(int(4 * k), int(8 * k), int(12 * k), int(9 * k)), R["o"])
    cv.fill(cv.rect(int(5 * k), int(8 * k), int(6 * k), int(8 * k)), R["tl"])
    cv.fill(cv.rect(int(10 * k), int(8 * k), int(11 * k), int(8 * k)), R["tl"])
    wisp = cv.poly([(8 * k, 2 * k), (10 * k, -2 * k), (12 * k, 1 * k), (9 * k, 3 * k)])
    cv.fill(wisp, R["tg"])
    cv.outline(R["o"])
    return cv


def relic_spear(s=16):  # the Spear of Ares, head wreathed in fire
    cv = Canvas(s, s)
    k = s / 16
    cv.line(2 * k, 14 * k, 10 * k, 6 * k, R["gd"])
    cv.line(2 * k + 1, 14 * k, 10 * k + 1, 6 * k, R["g"])
    head = cv.poly([(9 * k, 5 * k), (15 * k, 0), (11 * k, 7 * k)])
    fire = dilate(dilate(head)) & ~head & cv.rect(int(8 * k), 0, s, int(8 * k))
    cv.fill(fire, R["f"])
    cv.fill(head, R["sl"])
    cv.put(13 * k, 2 * k, R["wh"])
    cv.outline(R["o"])
    return cv


def relic_harpe(s=16):  # the sickle of Kronos
    cv = Canvas(s, s)
    k = s / 16
    blade = cv.ellipse(8 * k, 7 * k, 6.5 * k, 6.5 * k) & ~cv.ellipse(6 * k, 9 * k, 6 * k, 6 * k)
    blade &= cv.rect(0, 0, s, int(10 * k))
    cv.fill(blade, R["sl"])
    cv.fill(blade & ~dilate(~blade), R["wh"])
    cv.fill(cv.rect(int(11 * k), int(9 * k), int(12 * k), int(15 * k)), R["gd"])
    cv.fill(cv.rect(int(11 * k), int(9 * k), int(11 * k), int(15 * k)), R["g"])
    cv.fill(cv.rect(int(10 * k), int(9 * k), int(13 * k), int(10 * k)), R["fl"])
    cv.outline(R["o"])
    return cv


RELICS = dict(keraunos=relic_keraunos, trident=relic_trident, helm_of_darkness=relic_helm,
              spear_of_ares=relic_spear, harpe=relic_harpe)


def stamp(word="CONFIRMED") -> Canvas:
    """The Order Sheet stamp (Result screen): crit-red, rubber-worn, 2 px border."""
    w = font.width(word) * 2 + 16
    cv = Canvas(w, 32)
    cv.fill(cv.rect(0, 0, w - 1, 31), T["crit"])
    cv.erase(cv.rect(2, 2, w - 3, 29))
    cv.fill(cv.rect(4, 4, w - 5, 27), T["crit"])
    cv.erase(cv.rect(5, 5, w - 6, 26))
    m = Canvas(w, 32)
    font.display(m, 8, 9, word, [T["crit"]] * 4, T["crit"], scale=2, advance=12, stencil=False)
    cv.paste(m, 0, 0)
    # rubber wear: knock out a sparse speckle so it reads stamped, not printed
    rng = np.random.default_rng(3)
    for _ in range(w // 2):
        x, y = int(rng.integers(0, w)), int(rng.integers(0, 32))
        cv.erase(cv.rect(x, y, x, y))
    return cv

"""screens.py - full 256x192 screens: title, battle view and HUD, menus.

The battle top screen is a window on a 512x512 map (maps.py) seen from the
high camera; the bottom screens are composed from the ui kit so the runtime
can rebuild them from the same tiles.
"""

import math

import numpy as np

from px import Canvas, ramp, hexc, noise, dilate
from stage import W, H, YY, XX, taper, carve, bezier
import font
import ui
import units
import bosses

T = ui.T
# ---------------------------------------------------------------------------
# Title: key art (top) + save-slot menu (bottom).
# ---------------------------------------------------------------------------
def logo(cv, x, y):
    """Shield-cut-by-wedge mark (BRANDBOOK logo) in bronze + engraved type."""
    s = Canvas(32, 32)
    out = [(2, 2), (29, 2), (29, 21), (16, 30), (2, 21)]
    s.fill(s.poly(out), T["ink"])
    inner = s.poly([(4, 4), (27, 4), (27, 20), (16, 27), (4, 20)])
    s.shade(inner, [T["bronze_d"], T["bronze"], T["bronze_l"]], light=(-0.5, -0.8, 0.5), bevel=4,
            ambient=0.3)
    for off in (9, 14, 19):
        s.fill(s.rect(6, off, 15, off + 1) & inner, T["ink"])
    s.fill(s.poly([(15, 20), (26, 6), (26, 20)]) & inner, T["crit"])
    s.fill(s.stroke([(15, 20), (26, 6)], 1) & inner, T["ink"])
    cv.paste(s, x, y)
    font.display(cv, x + 38, y + 6, "WAR TABLE", [T["bronze_d"], T["bronze"], T["bronze_l"], T["surface"]],
                 T["ink"], scale=2, advance=12)


def title_top() -> Canvas:
    cv = Canvas(W, H)
    sky = ramp("#140a14", "#3a1420", "#6e2418", "#b0441c", "#e08a38", "#f6c870")
    cv.gradient(cv.rect(0, 0, W, H), sky, 0, 150, 1.0)
    # The watching god: a colossal helm in the clouds, lit from the sunset.
    cl = noise(W, H, 202, 20.0, 4)
    clouds = (cl + (YY - 30) / 160) > 0.62
    cv.shade(clouds & (YY < 120), ramp("#2a1018", "#4a1a20", "#7a2c22", "#b05030"),
             light=(0.0, 0.8, 0.4), bevel=5, dither=0.5, ambient=0.2)
    helm = cv.ellipse(128, 70, 44, 50) & (YY < 112)
    helm |= cv.poly([(86, 70), (170, 70), (160, 118), (140, 122), (116, 122), (96, 118)])
    hl = ramp("#1e0a10", "#2e1014", "#4a1a18", "#7a2c20")
    cv.shade(helm & (YY < 122), hl, light=(0.0, 0.7, 0.5), bevel=12, dither=0.5, ambient=0.15)
    crest = taper(cv, [(82, 64), (96, 26), (128, 14), (160, 26), (174, 64)], 9, ends=(4, 4))
    cv.shade(crest & ~cv.ellipse(128, 70, 40, 46), ramp("#2a080a", "#5a1212", "#8a2418"),
             light=(0, 0.7, 0.5), bevel=3, ambient=0.3)
    # the Corinthian T: eye slits joined by the gap between the cheek guards
    tgap = cv.poly([(92, 74), (164, 74), (164, 80), (133, 82), (131, 122), (125, 122), (123, 82),
                    (92, 80)])
    cv.fill(tgap, hexc("#0a0406"))
    for side in (-1, 1):
        cv.fill(cv.rect(128 + side * 20 - 4, 76, 128 + side * 20 + 3, 77), hexc("#ffb040"))
        cv.put(128 + side * 20, 76, hexc("#fff0b0"))
    cv.fill(cv.rect(127, 64, 128, 74), hl[3])
    rim = helm & ~np.roll(helm, 1, axis=0) & (YY < 122)
    cv.fill(rim, hexc("#c06a2c"))
    cv.fill(clouds & (YY > 96) & (YY < 124), ramp("#7a2c22")[0])
    # Acropolis on its hill, the army in ranks below: black against sunset.
    hill = cv.poly([(0, 150), (60, 128), (110, 120), (170, 124), (256, 140), (256, 192), (0, 192)])
    cv.fill(hill, hexc("#120a0c"))
    for k in range(8):
        x = 96 + k * 9
        cv.fill(cv.rect(x, 104, x + 3, 120), hexc("#120a0c"))
    cv.fill(cv.poly([(90, 104), (130, 92), (170, 104)]), hexc("#120a0c"))
    cv.fill(cv.rect(90, 104, 170, 106), hexc("#120a0c"))
    for k in range(9):
        cv.put(98 + k * 9, 109, hexc("#f6c870"))
    rng = np.random.default_rng(3)
    for row in range(5):
        y = 152 + row * 8
        for x in range(4 + (row % 2) * 3, W, 6 + row % 2):
            h = 5 + row
            cv.fill(cv.rect(x, y - h, x + 1, y), hexc("#0a0608"))
            cv.line(x + 1, y - h - 5 - row, x + 1, y - h, hexc("#2a1a18"))
            if rng.random() < 0.05:
                cv.fill(cv.rect(x + 2, y - h - 6, x + 6, y - h - 3), T["crit"])
    # Logo plate.
    plate = Canvas(W, H)
    logo(plate, 26, 140)
    cv.fill(cv.rect(18, 136, 238, 175), hexc("#0a0608"))
    cv.fill(cv.rect(18, 136, 238, 136), T["bronze"])
    cv.fill(cv.rect(18, 175, 238, 175), T["bronze_d"])
    cv.paste(plate, 0, 0)
    font.text(cv, 66, 164, "Plan the front. Watch it pay.", T["line"])
    return cv


def title_bottom() -> Canvas:
    cv = Canvas(W, H, fill=T["paper"])
    for y in range(4, H, 16):
        cv.put(6, y, T["line"])
        cv.put(249, y + 8, T["line"])
    cv.paste(ui.panel(W, 22, "iron"), 0, 0)
    font.text(cv, 8, 8, "FIELD LEDGER", T["bronze_l"], T["ink"])
    font.text(cv, 176, 8, "2 of 2 read", T["line"])
    for k, (slot, day, treas, army, civ, focus) in enumerate(
            (("Slot 1", "Day 12", "580", "24", "hellenes", True), ("Slot 2", "Day 9", "310", "16", "rome", False))):
        x = 8 + k * 124
        card = ui.panel(116, 76, "sheet")
        cv.paste(card, x, 30)
        if focus:
            cv.fill(cv.rect(x + 1, 31, x + 114, 32), T["accent"])
            cv.fill(cv.rect(x + 1, 31, x + 2, 104), T["accent"])
        font.text(cv, x + 10, 40, slot, T["accent"] if focus else T["ink"])
        font.text(cv, x + 64, 40, day, T["ink2"])
        font.text(cv, x + 10, 54, "Treasury", T["ink2"])
        font.display(cv, x + 10, 64, treas, [T["ink"], T["ink"], T["ink2"], T["ink2"]], T["paper"],
                     scale=2, advance=12, stencil=False)
        cv.paste(ui.ICONS["manpower"](), x + 74, 62)
        font.text(cv, x + 92, 66, army, T["ink"])
        fr = units.unit_frames(civ, "spear")["idle"][0]
        cv.paste(fr, x + 92, 80)
        cv.paste(units.unit_frames(civ, "spear")["idle"][1], x + 78, 80)
        font.text(cv, x + 10, 88, units.CIVS[civ]["name"], T["ink2"])
    cv.paste(ui.button(116, 20, "Continue", "focus"), 8, 116)
    cv.paste(ui.button(116, 20, "New Campaign", "normal"), 132, 116)
    cv.paste(ui.button(116, 20, "Settings", "normal"), 132, 140)
    cv.paste(ui.button(116, 20, "Vault", "disabled"), 8, 140)
    font.text(cv, 8, 168, "Slot 1 verified. Day 12, treasury 580.", T["ink"])
    cv.fill(cv.rect(0, 182, W, H), T["ink"])
    font.text(cv, 6, 184, "A confirm  B back  START settings", T["line"])
    return cv


# ---------------------------------------------------------------------------
# Battle: the field with both armies (mock-up) + the command panel.
# ---------------------------------------------------------------------------
_MAP_CACHE = {}


def battle_map(name):
    if name not in _MAP_CACHE:
        import maps
        _MAP_CACHE[name] = maps.MAPS[name]()
    return _MAP_CACHE[name]


def arena_map(god):
    """The arena recoloured for one god (what the palette swap shows)."""
    import maps
    base = battle_map("arena")
    cv = base.copy()
    swap = dict(zip([tuple(c) for c in sorted(base.colors())],
                    maps.arena_palette(god, [tuple(c) for c in sorted(base.colors())])))
    cv.recolor({k: v for k, v in swap.items() if k != v})
    return cv


def battle_top(map_name="plains", us="hellenes", them="rome", god=None, cam=(128, 160)) -> Canvas:
    """One frame of the high-camera battle: a window on the 512x512 map,
    squads in formation, and (for a boss fight) the god among them."""
    import squads
    import gods
    import effects
    field = arena_map(god) if god else battle_map(map_name)
    cv = field.crop(cam[0], cam[1], W, H)

    def put(img, x, y, flip=False):
        cv.paste(img, int(x), int(y), flip=flip)

    us_sheets = {c: squads.squad_sheet(us, c) for c in squads.SQUAD_CLASSES}
    them_sheets = {c: squads.squad_sheet(them, c) for c in squads.SQUAD_CLASSES}

    def squad(sheets, cls, anim, strength, f, x, y, flip):
        sh_, ix = sheets[cls]
        first, n = ix[f"{anim}_{strength}"]
        put(sh_.crop((first + f % n) * 32, 0, 32, 16), x, y, flip)

    rows = [("spear", 12), ("spear", 9), ("blade", 12), ("archer", 12)]
    for r, (cls, st) in enumerate(rows):
        for c in range(3):
            squad(us_sheets, cls, "walk" if r < 3 else "attack", st if c else 6, c + r, 20 + c * 34 - r * 6,
                  46 + r * 22 + c * 4, False)
            if not god:
                squad(them_sheets, cls, "attack" if r < 2 else "walk", st, c, 136 + c * 34 + r * 6,
                      46 + r * 22 + c * 4, True)
    squad(us_sheets, "cavalry", "walk", 12, 1, 14, 140, False)
    squad(us_sheets, "cavalry", "walk", 9, 2, 50, 150, False)
    gen, gix = squads.general_sheet(us)
    put(gen.crop(gix["command"][0] * 16, 0, 16, 16), 6, 24)
    if god:
        fr = gods.frames(god)["attack"][3]
        put(fr, 130, 34, True)
        fx = effects.EFFECTS[god](2)
        put(fx, 64, 96, False)
    else:
        squad(them_sheets, "cavalry", "attack", 12, 1, 212, 150, True)
        tg, tix = squads.general_sheet(them)
        put(tg.crop(0, 0, 16, 16), 232, 24, True)
    return cv


def battle_bottom(map_name="plains") -> Canvas:
    """Command panel: the whole 512x512 field as a 128x128 minimap (the high
    camera's other half), the divisions and the general's orders."""
    cv = Canvas(W, H, fill=T["iron"])
    cv.paste(ui.panel(W, 22, "iron"), 0, 0)
    font.text(cv, 8, 8, "FRONT WEST", T["bronze_l"], T["ink"])
    font.text(cv, 74, 8, "Turn 4/9", T["line"])
    for i, k in enumerate(("pause", "speed1", "speed2", "speed4")):
        x = 186 + i * 16
        if k == "speed2":
            cv.fill(cv.rect(x - 1, 3, x + 16, 19), T["accent"])
        cv.paste(ui.ICONS[k](), x, 3)
    # minimap: the real field at 1/4, armies as enamel dots, camera frame
    field = battle_map(map_name).image().convert("RGB").resize((124, 124), resample=0)
    mm = Canvas(124, 124)
    mm.a[..., :3] = np.array(field)
    mm.a[..., 3] = 255
    for k in range(12):
        x, y = 12 + (k % 3) * 8, 14 + (k // 3) * 7
        mm.fill(mm.rect(x, y, x + 3, y + 1), T["accent_l"])
        mm.fill(mm.rect(x + 66, y + 4, x + 69, y + 5), T["crit_l"])
    cam = mm.rect(32, 40, 95, 87) & ~mm.rect(33, 41, 94, 86)
    mm.fill(cam, T["bronze_l"])
    cv.fill(cv.rect(3, 25, 130, 152), T["ink"])
    cv.fill(cv.rect(4, 26, 129, 151), T["bronze_d"])
    cv.paste(mm, 5, 27)
    for x, y in ((3, 25), (130, 25), (3, 152), (130, 152)):
        cv.put(x, y, T["bronze_l"])
    divs = (("I Hoplites", 0.82, 0.7, False), ("II Hoplites", 0.55, 0.45, False),
            ("III Archers", 0.9, 0.8, False), ("IV Riders", 0.22, 0.15, True))
    for i, (name, hp, mo, crit) in enumerate(divs):
        y = 26 + i * 23
        cv.paste(ui.panel(118, 21, "sheet"), 134, y)
        font.text(cv, 140, y + 4, name, T["crit"] if crit else T["ink"])
        cv.paste(ui.bar(54, hp, "hp"), 140, y + 12)
        cv.paste(ui.bar(54, mo, "morale"), 194, y + 12)
    ab = (("Rally", "morale", 0), ("Charge", "battle", 0), ("Wall", "defense", 0.6), ("Volley", "attack", 1.0))
    for i, (name, icon, cd) in enumerate(ab):
        x = 134 + i * 30
        cv.paste(ui.panel(28, 26, "iron"), x, 120)
        cv.paste(ui.ICONS[icon](), x + 6, 125)
        if cd:
            for yy in range(124, 124 + int(18 * cd)):
                for xx in range(x + 4, x + 24):
                    if (xx + yy) % 2 == 0:
                        cv.put(xx, yy, T["ink"])
    cv.paste(ui.panel(W, 34, "boss"), 0, 158)
    cv.paste(ui.pins()["threat_critical"], 6, 167)
    font.text(cv, 24, 166, "Left flank broken.", T["surface"], T["ink"])
    font.text(cv, 24, 176, "IV Riders routing.", T["line"], T["ink"])
    return cv


# ---------------------------------------------------------------------------
# Boss intro card (bottom screen) - the monumental frame, once per act.
# ---------------------------------------------------------------------------
BOSS_INFO = {
    "zeus": ("ZEUS", "The Storm Throne", "keraunos", ("Storms strike the densest rank",
             "Phase 2: the sky floods"), ("Archers", "Spread lines")),
    "poseidon": ("POSEIDON", "The Abyssal Leviathan", "trident", ("Tides flood the low ground",
                 "Phase 2: the wave breaks"), ("Cavalry", "High ground")),
    "hades": ("HADES", "Gate of a Thousand Dead", "helm_of_darkness", ("The fallen rise for him",
              "Phase 2: the Styx overflows"), ("Fire", "Fast victory")),
    "ares": ("ARES", "The Bronze Colossus", "spear_of_ares", ("Rage: both armies hit harder",
             "Phase 2: the field burns"), ("Shieldwall", "Discipline")),
    "kronos": ("KRONOS", "Devourer of Ages", "harpe", ("Time slows your orders",
               "Phase 2: veterans grow young"), ("Veterans", "Speed")),
}


def boss_card(name) -> Canvas:
    title, epithet, relic, threats, weak = BOSS_INFO[name]
    cv = Canvas(W, H, fill=T["iron"])
    cv.paste(ui.panel(W, H, "boss"), 0, 0)
    # portrait: the boss stage's focal crop in a bronze frame
    stage = bosses.BOSSES[name].paint()
    focus = {"zeus": (76, 22), "poseidon": (54, 14), "hades": (96, 22), "ares": (96, 0),
             "kronos": (96, 20)}[name]
    crop = stage.crop(focus[0], focus[1], 64, 64)
    cv.fill(cv.rect(9, 9, 76, 76), T["bronze_d"])
    cv.fill(cv.rect(10, 10, 75, 75), T["bronze"])
    cv.paste(crop, 11, 11)
    cv.fill(cv.rect(10, 10, 75, 10), T["bronze_l"])
    font.display(cv, 86, 12, title, [T["bronze_d"], T["bronze"], T["bronze_l"], T["surface"]], T["ink"],
                 scale=2, advance=12)
    font.text(cv, 86, 34, epithet, T["line"])
    cv.paste(ui.bar(158, 1.0, "boss"), 86, 46)
    font.text(cv, 86, 60, "Relic", T["ink2"])
    cv.paste(ui.RELICS[relic](), 116, 56)
    font.text(cv, 136, 60, relic.replace("_", " ").title(), T["bronze_l"])
    cv.paste(ui.panel(240, 52, "iron"), 8, 84)
    font.text(cv, 16, 92, "THREAT", T["crit_l"])
    for i, t in enumerate(threats):
        cv.paste(ui.pins()["threat_critical" if i else "threat_high"], 14, 101 + i * 14)
        font.text(cv, 32, 105 + i * 14, t, T["surface"])
    cv.paste(ui.panel(240, 40, "sheet"), 8, 140)
    font.text(cv, 16, 148, "WEAK TO", T["accent"])
    font.text(cv, 16, 162, "  /  ".join(weak), T["ink"])
    cv.paste(ui.ICONS["battle"](), 222, 152)
    return cv


# ---------------------------------------------------------------------------
# Result: the Order Sheet with the CONFIRMED stamp.
# ---------------------------------------------------------------------------
def result_bottom() -> Canvas:
    cv = Canvas(W, H, fill=T["paper"])
    cv.paste(ui.panel(W - 8, H - 30, "sheet"), 4, 4)
    font.text(cv, 14, 14, "ORDER SHEET  -  FRONT WEST", T["ink"])
    font.text(cv, 14, 24, "Day 12, turn 9 of 9", T["ink2"])
    rows = (("gold", "Plunder", "+300"), ("manpower", "Veterans", "+3"), ("casualties", "Fallen", "-7"),
            ("tier", "Front tier", "2 > 3"))
    for i, (ic, lab, val) in enumerate(rows):
        y = 40 + i * 22
        cv.paste(ui.ICONS[ic](), 14, y)
        font.text(cv, 36, y + 5, lab, T["ink"])
        font.display(cv, 236 - len(val) * 12, y + 1, val,
                     [T["ink"], T["crit"] if val.startswith("-") else T["ink"], T["ink2"], T["ink2"]],
                     T["paper"], scale=2, advance=12, stencil=False)
        for x in range(36, 236, 3):
            cv.put(x, y + 19, T["line"])
    st = ui.stamp()
    rot = st.image().rotate(8, expand=True, resample=0)
    tmp = Canvas(rot.width, rot.height)
    tmp.a[:] = np.array(rot)
    cv.paste(tmp, 120, 112)
    cv.paste(ui.button(116, 20, "Continue", "focus"), 8, 168)
    cv.paste(ui.button(116, 20, "Base", "normal"), 132, 168)
    return cv


# ---------------------------------------------------------------------------
# Base: recruit sheet (bottom) - the chest numeral is the one bold move.
# ---------------------------------------------------------------------------
def recruit_bottom(civ="hellenes") -> Canvas:
    cv = Canvas(W, H, fill=T["paper"])
    cv.paste(ui.panel(W, 22, "iron"), 0, 0)
    for i, tab in enumerate(("Recruit", "Formation", "Tier")):
        x = 6 + i * 82
        if i == 0:
            cv.fill(cv.rect(x, 4, x + 78, 21), T["paper"])
            font.text(cv, x + 39 - font.width(tab) // 2, 8, tab, T["ink"])
        else:
            font.text(cv, x + 39 - font.width(tab) // 2, 8, tab, T["line"])
    cls = (("spear", "Hoplite", "40"), ("blade", "Swordsman", "55"), ("archer", "Archer", "35"),
           ("cavalry", "Rider", "90"))
    for i, (c, name, cost) in enumerate(cls):
        x, y = 6 + (i % 2) * 124, 28 + (i // 2) * 56
        cv.paste(ui.panel(120, 52, "sheet"), x, y)
        f = units.unit_frames(civ, c)["idle"][0]
        cv.paste(f, x + 8 + (16 - f.w // 2) if f.w == 16 else x + 4, y + 10 if f.w == 16 else y + 14)
        font.text(cv, x + 42, y + 10, name, T["ink"])
        cv.paste(ui.ICONS["gold"](), x + 40, y + 22)
        font.text(cv, x + 58, y + 26, cost, T["ink"])
        cv.paste(ui.button(28, 14, "+5", "focus" if i == 0 else "normal"), x + 86, y + 32)
    font.text(cv, 10, 146, "Chest", T["ink2"])
    font.display(cv, 10, 156, "240/300", [T["ink"], T["ink"], T["ink2"], T["ink2"]], T["paper"],
                 scale=2, advance=12, stencil=False)
    cv.paste(ui.button(100, 20, "Deploy", "focus"), 150, 158)
    return cv

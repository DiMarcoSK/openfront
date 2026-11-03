#!/usr/bin/env python3
"""export.py - build every War Table art asset into DS-ready form.

    python game/tools/art/export.py            (from the repo root)

Writes, deterministically:
  game/art/ds/bg/*.png  + .grit   indexed BGs (8bpp: 512x512 battle maps, boss stages, screens)
  game/art/ds/obj/*.png + .grit   4bpp OAM sheets (squads, gods, effects, portraits, icons)
  game/art/ds/art_index.h         animation ranges, palette slots, cycle groups
  game/art/preview/*.png          4x previews + contact sheets for review
  game/art/ds/manifest.json       per-asset colours, sizes and byte estimates

Indexed PNGs carry their exact palette: every colour was painted from a
DS-snapped ramp, so the mapping is lossless and grit keeps the order (the
palette-cycle groups stay contiguous).
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from px import Canvas, bgr555  # noqa: E402
import bosses  # noqa: E402
import effects  # noqa: E402
import gods  # noqa: E402
import maps  # noqa: E402
import squads  # noqa: E402
import font  # noqa: E402
import screens  # noqa: E402
import ui  # noqa: E402
import units  # noqa: E402

GAME = HERE.parents[1]
DS = GAME / "art" / "ds"
PREVIEW = GAME / "art" / "preview"

GRIT_BG8 = "# 8bpp tiled BG, flip-reduced map in screenblocks, own palette\n-gB8 -gt -m -mRtf -mLs -p\n-ftc -fh\n"
GRIT_BG4 = "# 4bpp tiled BG, flip-reduced map, one 16-colour palette\n-gB4 -gt -m -mRtf -p\n-ftc -fh\n"
GRIT_OBJ = "# 4bpp OAM sheet, {w}x{h} blocks in metatile order\n-gB4 -gt -Mw{mw} -Mh{mh} -p -pn16\n-ftc -fh\n"
GRIT_FONT = "# 8x8 4bpp font tiles, ASCII 32..126\n-gB4 -gt -p -pn16\n-ftc -fh\n"

manifest = {}


def palette_for(cv: Canvas, groups: list, limit: int, first=None) -> list:
    """Palette = named ramps first (contiguous cycle groups), then the rest."""
    used = cv.colors()
    out = [] if first is None else [first]
    for g in groups:
        for c in g:
            c = tuple(c)
            if c in used and c not in out:
                out.append(c)
    rest = sorted(used - set(out), key=lambda c: (0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]))
    out += rest
    assert len(out) <= limit, f"{len(out)} colours > {limit}"
    return out


def save_indexed(cv: Canvas, pal: list, path: Path, transparent=True):
    h, w = cv.h, cv.w
    idx = np.zeros((h, w), np.uint8)
    lut = {c: i for i, c in enumerate(pal)}
    op = cv.opaque()
    rgb = cv.a[..., :3]
    for y in range(h):
        for x in range(w):
            if op[y, x]:
                c = (int(rgb[y, x, 0]), int(rgb[y, x, 1]), int(rgb[y, x, 2]))
                if transparent and c == tuple(pal[0]) and pal[0] == (0, 0, 0):
                    raise ValueError(f"{path.name}: opaque pixel uses the transparent slot")
                idx[y, x] = lut[c]
            elif not transparent:
                raise ValueError(f"{path.name}: hole in an opaque BG at {x},{y}")
    img = Image.fromarray(idx, "P")
    flat = [v for c in pal for v in c]
    img.putpalette(flat + [0] * (768 - len(flat)))
    path.parent.mkdir(parents=True, exist_ok=True)
    img.save(path, optimize=False)


def unique_tiles(cv: Canvas) -> int:
    """Tile count after flip reduction: what grit -mRtf leaves in VRAM."""
    seen = set()
    a = cv.a
    for ty in range(0, cv.h, 8):
        for tx in range(0, cv.w, 8):
            t = a[ty:ty + 8, tx:tx + 8].tobytes()
            if t in seen:
                continue
            blk = a[ty:ty + 8, tx:tx + 8]
            variants = {blk[:, ::-1].tobytes(), blk[::-1].tobytes(), blk[::-1, ::-1].tobytes()}
            if variants & seen:
                continue
            seen.add(t)
    return len(seen)


def bg(name: str, cv: Canvas, bpp: int, groups=(), cycles=None):
    limit = 256 if bpp == 8 else 16
    pal = palette_for(cv, list(groups), limit)
    path = DS / "bg" / f"{name}.png"
    save_indexed(cv, pal, path, transparent=False)
    (DS / "bg" / f"{name}.grit").write_text(GRIT_BG8 if bpp == 8 else GRIT_BG4)
    tiles = unique_tiles(cv)
    vram = tiles * (64 if bpp == 8 else 32) + (cv.w // 8) * (cv.h // 8) * 2
    entry = dict(kind="bg", bpp=bpp, colours=len(pal), tiles=tiles, vram_bytes=vram, w=cv.w, h=cv.h)
    if cycles:
        rng = {}
        for gname, ramps in cycles.items():
            cols = [tuple(c) for r in ramps for c in r if tuple(c) in pal]
            ix = sorted(pal.index(c) for c in cols)
            if ix:
                rng[gname] = (ix[0], ix[-1] - ix[0] + 1)
        entry["cycles"] = rng
    manifest[name] = entry
    cv.save(PREVIEW / f"{name}.png", 3 if cv.w <= 256 else 1)
    return pal


def obj_sheet(name: str, cv: Canvas, pal: list, frame: int, extra=None, fh=None, per_frame=1):
    """`frame` x `fh` OAM blocks; `per_frame` blocks make one animation frame
    (a 128x128 god is 4 blocks of 64x64)."""
    fh = fh or frame
    path = DS / "obj" / f"{name}.png"
    save_indexed(cv, pal, path, transparent=True)
    grit = GRIT_FONT if frame == 8 else GRIT_OBJ.format(w=frame, h=fh, mw=frame // 8, mh=fh // 8)
    (DS / "obj" / f"{name}.grit").write_text(grit)
    blocks = (cv.w // frame) * (cv.h // fh)
    entry = dict(kind="obj", frame=frame, fh=fh, frames=blocks // per_frame, per_frame=per_frame,
                 bytes=blocks * frame * fh // 2)
    if extra:
        entry.update(extra)
    manifest[name] = entry


indexes = {}
arena_pals = {}


def build():
    for d in (DS / "bg", DS / "obj", PREVIEW):
        d.mkdir(parents=True, exist_ok=True)
        for f in d.glob("*"):
            f.unlink()

    # -- bosses: 8bpp stages, cycle groups first in the palette ------------
    for name, mod in bosses.BOSSES.items():
        cv = mod.paint()
        cyc = {g: [mod.PAL[k] for k in ks] for g, ks in mod.CYCLES.items()}
        groups = [mod.PAL[k] for ks in mod.CYCLES.values() for k in ks]
        bg(f"boss_{name}", cv, 8, groups, cyc)
        bg(f"card_{name}", screens.boss_card(name), 8)

    # -- battle maps: 512x512 8bpp, water ramp first (palette cycle) -------
    for name in maps.MAPS:
        cv = screens.battle_map(name)
        ramps = maps.ARENA_BASE if name == "arena" else maps.BIOMES[name]
        order = [ramps[k] for k in ("water", "base", "dirt", "rock", "leaf", "cliff", "marble", "wall", "roof")]
        pal = bg(f"map_{name}", cv, 8, order, {"water": [ramps["water"]]})
        if name == "arena":
            arena_pals["base"] = pal
    for god in maps.ARENA_GODS:
        arena_pals[god] = maps.arena_palette(god, arena_pals["base"])

    # -- screens -----------------------------------------------------------
    bg("title_top", screens.title_top(), 8)
    bg("title_bottom", screens.title_bottom(), 8)
    for name in ("plains", "desert", "highlands"):
        bg(f"battle_bottom_{name}", screens.battle_bottom(name), 8)
    bg("result_bottom", screens.result_bottom(), 8)
    bg("recruit_bottom", screens.recruit_bottom(), 8)
    bg("battle_mock", screens.battle_top("plains"), 8)

    # -- the army at battle scale: squads (32x16) and generals (16x16) -------
    squad_index = gen_index = None
    for civ in units.CIVS:
        pal = units.palette(civ)
        for cls in squads.SQUAD_CLASSES:
            cv, squad_index = squads.squad_sheet(civ, cls)
            obj_sheet(f"squad_{civ}_{cls}", cv, pal, 32, fh=16)
        cv, gen_index = squads.general_sheet(civ)
        obj_sheet(f"squad_{civ}_general", cv, pal, 16)
    indexes["squad"] = squad_index
    indexes["squad_general"] = gen_index

    # -- the gods on the field: 128x128 = 4 x 64x64 per frame + effects ------
    for god in gods.GODS:
        pal = gods.palette(god)
        cv, gindex, seq = gods.sheet(god)
        obj_sheet(f"god_{god}", cv, pal, 64, per_frame=4)
        indexes["god"] = gindex
        obj_sheet(f"fx_{god}", effects.sheet(god), pal, 64)
        strip = Canvas(128 * len(seq), 128, fill=(70, 90, 50))
        for i, f in enumerate(seq):
            strip.paste(f, i * 128, 0)
        strip.save(PREVIEW / f"god_{god}.png", 1)

    # -- portraits: the 16/32 px close-up figures for menus and cards -------
    anim_index = {}
    for civ in units.CIVS:
        pal = units.palette(civ)
        for cls in units.CLASSES:
            cv, index = units.sheet(civ, cls)
            obj_sheet(f"unit_{civ}_{cls}", cv, pal, units.FRAME_SIZE[cls])
            anim_index[cls] = index
        units_preview = [units.sheet(civ, c)[0] for c in units.CLASSES]
        w = max(r.w for r in units_preview)
        board = Canvas(w, sum(r.h + 2 for r in units_preview), fill=(36, 40, 34))
        y = 0
        for r in units_preview:
            board.paste(r, 0, y)
            y += r.h + 2
        board.save(PREVIEW / f"units_{civ}.png", 4)

    # -- icons / relics / pins / font --------------------------------------
    names = list(ui.ICONS)
    sheet = Canvas(16 * len(names), 16)
    for i, n in enumerate(names):
        sheet.paste(ui.ICONS[n](), i * 16, 0)
    obj_sheet("icons", sheet, ui.ICON_PAL, 16, dict(order=names))
    sheet.save(PREVIEW / "icons.png", 6)

    rn = list(ui.RELICS)
    sheet = Canvas(16 * len(rn), 16)
    big = Canvas(32 * len(rn), 32)
    for i, n in enumerate(rn):
        sheet.paste(ui.RELICS[n](), i * 16, 0)
        big.paste(ui.RELICS[n](32), i * 32, 0)
    obj_sheet("relics", sheet, ui.RELIC_PAL, 16, dict(order=rn))
    obj_sheet("relics32", big, ui.RELIC_PAL, 32, dict(order=rn))
    sheet.save(PREVIEW / "relics.png", 6)
    big.save(PREVIEW / "relics32.png", 4)

    pins = ui.pins()
    sheet = Canvas(16 * len(pins), 16)
    for i, p in enumerate(pins.values()):
        sheet.paste(p, i * 16, 0)
    obj_sheet("pins", sheet, ui.UI_PAL, 16, dict(order=list(pins)))

    fs = font.sheet(ui.T["ink"])
    obj_sheet("font", fs, [(0, 0, 0), ui.T["ink"]] + [ui.T["ink2"]] * 0, 8)
    font.sheet(ui.T["ink"], ui.T["paper"]).save(PREVIEW / "font.png", 4)

    kit = Canvas(256, 120, fill=ui.T["iron"])
    kit.paste(ui.panel(96, 48, "sheet"), 4, 4)
    kit.paste(ui.panel(72, 48, "iron"), 104, 4)
    kit.paste(ui.panel(72, 48, "boss"), 180, 4)
    for i, s in enumerate(("normal", "focus", "pressed", "disabled")):
        kit.paste(ui.button(60, 16, "Deploy", s), 4 + i * 63, 58)
    for i, (k, v) in enumerate((("hp", 0.7), ("morale", 0.4), ("boss", 0.9))):
        kit.paste(ui.bar(80, v, k), 4 + i * 84, 80)
    kit.paste(ui.stamp(), 4, 88)
    font.display(kit, 140, 96, "WAR TABLE", [ui.T["bronze_d"], ui.T["bronze"], ui.T["bronze_l"],
                                             ui.T["surface"]], ui.T["ink"])
    kit.save(PREVIEW / "ui_kit.png", 4)

    write_header(anim_index)
    write_asset_table()
    write_palettes()
    (DS / "manifest.json").write_text(json.dumps(manifest, indent=1, sort_keys=True))
    report()


def write_header(anim_index):
    lines = ["/* art_index.h - generated by tools/art/export.py; DO NOT EDIT.",
             " * Animation ranges (frame index into each unit sheet), the OAM palette",
             " * slot per sheet family and the palette-cycle groups of each boss stage.",
             " */",
             "#ifndef WAR_TABLE_ART_INDEX_H", "#define WAR_TABLE_ART_INDEX_H", "",
             "#include <stdint.h>", "",
             "typedef struct { uint8_t first, count; } art_range_t;", "",
             "enum { ART_IDLE, ART_WALK, ART_ATTACK, ART_COMMAND, ART_HURT, ART_DEATH, ART_ANIMS };",
             ""]
    order = ["idle", "walk", "attack", "command", "hurt", "death"]
    for cls, idx in anim_index.items():
        rows = []
        for a in order:
            f, n = idx.get(a, idx["idle"])
            rows.append(f"{{{f}, {n}}}")
        lines.append(f"static const art_range_t ART_ANIM_{cls.upper()}[ART_ANIMS] = "
                     f"{{{', '.join(rows)}}};")
    lines += ["", "/* Battle-scale squads: {first, count} per strength (12/9/6/3 men) x anim. */",
              "enum { ART_SQ_IDLE, ART_SQ_WALK, ART_SQ_ATTACK, ART_SQ_ANIMS };",
              "enum { ART_SQ_12, ART_SQ_9, ART_SQ_6, ART_SQ_3, ART_SQ_STRENGTHS };"]
    sq = indexes["squad"]
    rows = []
    for st in (12, 9, 6, 3):
        rows.append("{" + ", ".join("{%d, %d}" % sq[f"{a}_{st}"] for a in ("idle", "walk", "attack")) + "}")
    lines.append("static const art_range_t ART_SQUAD[ART_SQ_STRENGTHS][ART_SQ_ANIMS] = {" + ", ".join(rows) + "};")
    g = indexes["squad_general"]
    lines.append("static const art_range_t ART_SQUAD_GENERAL[3] = {" +
                 ", ".join("{%d, %d}" % g[a] for a in ("idle", "walk", "command")) + "};")
    lines += ["", "/* Gods: 128x128 frames, each 4 contiguous 64x64 blocks (TL, TR, BL, BR). */",
              "enum { ART_GOD_IDLE, ART_GOD_WALK, ART_GOD_ATTACK, ART_GOD_SPECIAL, ART_GOD_HURT, ART_GOD_DEATH,",
              "       ART_GOD_ANIMS };"]
    gi = indexes["god"]
    lines.append("static const art_range_t ART_GOD[ART_GOD_ANIMS] = {" +
                 ", ".join("{%d, %d}" % gi[a] for a in gods.ANIMS) + "};")
    lines.append(f"#define ART_FX_FRAMES {effects.FRAMES}u")
    lines += ["", "/* OAM palette slots (16 colours each). Civ order = ART_CIV_*. */",
              "enum { ART_CIV_HELLENES, ART_CIV_ROME, ART_CIV_CARTHAGE, ART_CIV_ASSYRIA,",
              "       ART_PAL_ICONS, ART_PAL_RELICS, ART_PAL_PINS, ART_PAL_GOD, ART_PAL_FLASH };", "",
              "/* Unit palette roles: write ART_ROLE_* of a civ palette to recolour one role. */"]
    for i, r in enumerate(units.ROLES):
        lines.append(f"#define ART_ROLE_{r.upper()} {i}u")
    lines += ["", "/* Palette-cycle groups: {first index, count} in each 8bpp BG palette. */"]
    for name, e in manifest.items():
        for g, (f, n) in e.get("cycles", {}).items():
            lines.append(f"#define ART_CYCLE_{name.upper()}_{g.upper()} {f}u, {n}u")
    lines += ["", "#endif", ""]
    (DS / "art_index.h").write_text("\n".join(lines))


def write_asset_table():
    """art_assets.h: one table per kind over the grit symbols (any consumer
    - the artview ROM today, render.c later - indexes these, never names)."""
    bgs = sorted(k for k, v in manifest.items() if v["kind"] == "bg")
    objs = sorted(k for k, v in manifest.items() if v["kind"] == "obj")
    L = ["/* art_assets.h - generated by tools/art/export.py; DO NOT EDIT.",
         " * Include in exactly one .c file: it defines the asset tables.", " */",
         "#ifndef WAR_TABLE_ART_ASSETS_H", "#define WAR_TABLE_ART_ASSETS_H", "",
         "#include <stdint.h>"]
    L += [f'#include "{k}.h"' for k in bgs + objs]
    L += ["", "typedef struct {", "\tconst void *tiles; uint32_t tiles_len;",
          "\tconst void *map; uint32_t map_len;", "\tconst void *pal; uint32_t pal_len;",
          "\tuint8_t bpp;", "} art_bg_t;", "",
          "typedef struct { const void *gfx; const void *pal; uint16_t frames; uint8_t w, h, per_frame; } art_obj_t;",
          "", "enum {"]
    L += [f"\tART_BG_{k.upper()}," for k in bgs] + ["\tART_BG_COUNT", "};", "", "enum {"]
    L += [f"\tART_OBJ_{k.upper()}," for k in objs] + ["\tART_OBJ_COUNT", "};", "",
                                                       "static const art_bg_t ART_BG[ART_BG_COUNT] = {"]
    for k in bgs:
        L.append(f"\t{{{k}Tiles, {k}TilesLen, {k}Map, {k}MapLen, {k}Pal, {k}PalLen, "
                 f"{manifest[k]['bpp']}}},")
    L += ["};", "", "static const art_obj_t ART_OBJ[ART_OBJ_COUNT] = {"]
    for k in objs:
        e = manifest[k]
        L.append(f"\t{{{k}Tiles, {k}Pal, {e['frames']}, {e['frame']}, {e['fh']}, {e['per_frame']}}},")
    L += ["};", "", "#endif", ""]
    (DS / "art_assets.h").write_text("\n".join(L))


def write_palettes():
    """art_palettes.h: the god arena's palette per god (same tiles, new sky)."""
    L = ["/* art_palettes.h - generated by tools/art/export.py; DO NOT EDIT.",
         " * One 256-entry BGR555 palette per god for map_arena (load instead of its own). */",
         "#ifndef WAR_TABLE_ART_PALETTES_H", "#define WAR_TABLE_ART_PALETTES_H", "", "#include <stdint.h>", ""]
    for god in maps.ARENA_GODS:
        pal = arena_pals[god] + [(0, 0, 0)] * (256 - len(arena_pals[god]))
        vals = ", ".join(f"0x{bgr555(c):04x}" for c in pal)
        L.append(f"static const uint16_t ART_ARENA_PAL_{god.upper()}[256] = {{{vals}}};")
    L += ["", "#endif", ""]
    (DS / "art_palettes.h").write_text("\n".join(L))


def report():
    bgs = {k: v for k, v in manifest.items() if v["kind"] == "bg"}
    objs = {k: v for k, v in manifest.items() if v["kind"] == "obj"}
    worst = max(bgs.items(), key=lambda kv: kv[1]["vram_bytes"])
    print(f"bg  {len(bgs):3d} assets; worst VRAM {worst[0]} = {worst[1]['vram_bytes'] // 1024} KB "
          f"({worst[1]['tiles']} tiles, {worst[1]['colours']} colours)")
    print(f"obj {len(objs):3d} sheets; {sum(v['bytes'] for v in objs.values()) // 1024} KB of frames total")
    for k, v in bgs.items():
        assert v["tiles"] < 1024, f"{k}: {v['tiles']} tiles > the 1024 a text BG can index"
        assert v["vram_bytes"] <= 72 * 1024, f"{k} exceeds its VRAM slot"


if __name__ == "__main__":
    build()

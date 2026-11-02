#!/usr/bin/env python3
"""Generate deterministic 1x art boards and the DS-sized pixel-art atlas.

The screen boards are visual references, not runtime screenshots. The atlas is
an 8x8-aligned, 4bpp-ready source asset compiled by the existing grit wildcard.
"""

from pathlib import Path
from PIL import Image, ImageDraw


ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "game" / "data"
ART = ROOT / "game" / "art"

# Palette transcribed from tokens.json; RGB literals belong only to this art
# source generator (runtime C colors remain generated in contracts/tokens.h).
PAPER = (237, 230, 208)
SURFACE = (246, 241, 227)
INK = (30, 36, 28)
INK2 = (79, 84, 69)
LINE = (201, 191, 164)
GREEN = (46, 90, 39)
WHITE = SURFACE
AMBER = (122, 74, 0)
RED = (140, 27, 21)
TRANSPARENT = (0, 0, 0, 0)

FONT = {
    "A": ("01110", "10001", "10001", "11111", "10001", "10001", "10001"),
    "B": ("11110", "10001", "10001", "11110", "10001", "10001", "11110"),
    "C": ("01111", "10000", "10000", "10000", "10000", "10000", "01111"),
    "D": ("11110", "10001", "10001", "10001", "10001", "10001", "11110"),
    "E": ("11111", "10000", "10000", "11110", "10000", "10000", "11111"),
    "F": ("11111", "10000", "10000", "11110", "10000", "10000", "10000"),
    "G": ("01111", "10000", "10000", "10111", "10001", "10001", "01111"),
    "H": ("10001", "10001", "10001", "11111", "10001", "10001", "10001"),
    "I": ("11111", "00100", "00100", "00100", "00100", "00100", "11111"),
    "J": ("00111", "00010", "00010", "00010", "10010", "10010", "01100"),
    "K": ("10001", "10010", "10100", "11000", "10100", "10010", "10001"),
    "L": ("10000", "10000", "10000", "10000", "10000", "10000", "11111"),
    "M": ("10001", "11011", "10101", "10101", "10001", "10001", "10001"),
    "N": ("10001", "11001", "10101", "10011", "10001", "10001", "10001"),
    "O": ("01110", "10001", "10001", "10001", "10001", "10001", "01110"),
    "P": ("11110", "10001", "10001", "11110", "10000", "10000", "10000"),
    "Q": ("01110", "10001", "10001", "10001", "10101", "10010", "01101"),
    "R": ("11110", "10001", "10001", "11110", "10100", "10010", "10001"),
    "S": ("01111", "10000", "10000", "01110", "00001", "00001", "11110"),
    "T": ("11111", "00100", "00100", "00100", "00100", "00100", "00100"),
    "U": ("10001", "10001", "10001", "10001", "10001", "10001", "01110"),
    "V": ("10001", "10001", "10001", "10001", "10001", "01010", "00100"),
    "W": ("10001", "10001", "10001", "10101", "10101", "10101", "01010"),
    "X": ("10001", "10001", "01010", "00100", "01010", "10001", "10001"),
    "Y": ("10001", "10001", "01010", "00100", "00100", "00100", "00100"),
    "Z": ("11111", "00001", "00010", "00100", "01000", "10000", "11111"),
    "0": ("01110", "10001", "10011", "10101", "11001", "10001", "01110"),
    "1": ("00100", "01100", "00100", "00100", "00100", "00100", "01110"),
    "2": ("01110", "10001", "00001", "00010", "00100", "01000", "11111"),
    "3": ("11110", "00001", "00001", "01110", "00001", "00001", "11110"),
    "4": ("00010", "00110", "01010", "10010", "11111", "00010", "00010"),
    "5": ("11111", "10000", "10000", "11110", "00001", "00001", "11110"),
    "6": ("01110", "10000", "10000", "11110", "10001", "10001", "01110"),
    "7": ("11111", "00001", "00010", "00100", "01000", "01000", "01000"),
    "8": ("01110", "10001", "10001", "01110", "10001", "10001", "01110"),
    "9": ("01110", "10001", "10001", "01111", "00001", "00001", "01110"),
    "+": ("00000", "00100", "00100", "11111", "00100", "00100", "00000"),
    "-": ("00000", "00000", "00000", "11111", "00000", "00000", "00000"),
    "/": ("00001", "00010", "00010", "00100", "01000", "01000", "10000"),
    ":": ("00000", "00100", "00100", "00000", "00100", "00100", "00000"),
    ".": ("00000", "00000", "00000", "00000", "00000", "00110", "00110"),
    "[": ("01110", "01000", "01000", "01000", "01000", "01000", "01110"),
    "]": ("01110", "00010", "00010", "00010", "00010", "00010", "01110"),
    ">": ("10000", "01000", "00100", "00010", "00100", "01000", "10000"),
    "<": ("00001", "00010", "00100", "01000", "00100", "00010", "00001"),
    "|": ("00100", "00100", "00100", "00100", "00100", "00100", "00100"),
    "'": ("00100", "00100", "00010", "00000", "00000", "00000", "00000"),
    " ": ("00000",) * 7,
}


def text(draw, xy, value, color=INK, scale=1, advance=8):
    x, y = xy
    for ch in value.upper():
        glyph = FONT.get(ch, FONT[" "])
        for gy, row in enumerate(glyph):
            for gx, bit in enumerate(row):
                if bit == "1":
                    px, py = x + gx * scale, y + gy * scale
                    draw.rectangle((px, py, px + scale - 1, py + scale - 1),
                                   fill=color)
        x += advance * scale


def seam(draw, y, x0=8, x1=248):
    draw.line((x0, y, x1, y), fill=LINE)
    for x in range(x0 + 4, x1, 12):
        draw.point((x, y + 1), fill=(220, 211, 189))


def pin(draw, cx, cy, radius, color, kind):
    # Enamel pin silhouette: engraved outline, metal glint, directional point.
    draw.ellipse((cx-radius, cy-radius, cx+radius, cy+radius), fill=INK)
    draw.ellipse((cx-radius+1, cy-radius+1, cx+radius-1, cy+radius-1), fill=color)
    draw.polygon([(cx-radius//2, cy+radius-1), (cx+radius//2, cy+radius-1),
                  (cx, cy+radius+3)], fill=INK)
    if kind == "low":
        draw.line((cx-2, cy, cx+2, cy), fill=WHITE)
    elif kind == "medium":
        draw.point((cx, cy-2), fill=WHITE)
        draw.line((cx-2, cy+1, cx+2, cy+1), fill=WHITE)
    elif kind == "high":
        draw.polygon([(cx, cy-3), (cx+3, cy+2), (cx-3, cy+2)], fill=WHITE)
    else:
        draw.line((cx, cy-3, cx, cy+1), fill=WHITE, width=1)
        draw.point((cx, cy+3), fill=WHITE)


def shield(draw, x, y, size=32):
    # Shield cut by an ascending wedge: three engraved front lines, growth slope.
    s = size
    outline = [(x+2, y+2), (x+s-3, y+2), (x+s-3, y+s*2//3),
               (x+s//2, y+s-2), (x+2, y+s*2//3)]
    draw.polygon(outline, fill=INK)
    inner = [(x+4, y+4), (x+s-5, y+4), (x+s-5, y+s*2//3-1),
             (x+s//2, y+s-5), (x+4, y+s*2//3-1)]
    draw.polygon(inner, fill=PAPER)
    for off in (10, 15, 20):
        draw.line((x+6, y+off, x+s//2-1, y+off), fill=GREEN, width=2)
    draw.polygon([(x+s//2-1, y+s*2//3-2), (x+s-6, y+7),
                  (x+s-6, y+s*2//3-2)], fill=GREEN)
    draw.line((x+s//2-1, y+s*2//3-2, x+s-6, y+7), fill=INK)


def art_atlas():
    image = Image.new("RGBA", (128, 64), TRANSPARENT)
    d = ImageDraw.Draw(image)
    shield(d, 0, 0, 32)
    # Four threat pins on a 16 px grid; labels are deliberately not baked in.
    pin(d, 48, 13, 6, GREEN, "low")
    pin(d, 72, 13, 6, AMBER, "medium")
    pin(d, 96, 13, 6, INK2, "high")
    pin(d, 120, 13, 6, RED, "critical")
    # Supply marker and 8x8 oilcloth stitch motif for quiet panel separators.
    pin(d, 48, 43, 6, AMBER, "medium")
    d.line((64, 32, 127, 32), fill=LINE)
    for x in (64, 80, 96, 112):
        d.line((x, 35, x+5, 35), fill=INK2)
        d.point((x+7, 35), fill=LINE)
    image.save(DATA / "war_table_art.png", optimize=False)


def title_board():
    image = Image.new("RGB", (256, 384), PAPER)
    d = ImageDraw.Draw(image)
    # Quiet canvas grain: sparse 1 px fibers leave every text field clean.
    for y in range(4, 384, 16):
        d.line((8, y, 12, y), fill=(225, 217, 197))
        d.point((243, y+3), fill=(225, 217, 197))

    # TOP: quiet brand line and focused ledger sheet.
    shield(d, 12, 12, 24)
    text(d, (46, 17), "WAR TABLE", INK, scale=1, advance=8)
    text(d, (174, 19), "FIELD LEDGER", INK2, scale=1, advance=8)
    seam(d, 43, 12, 244)
    d.rounded_rectangle((12, 54, 244, 180), radius=2, fill=SURFACE, outline=LINE)
    text(d, (24, 66), "SLOT 1", GREEN)
    text(d, (24, 84), "DAY 12", INK2)
    text(d, (24, 106), "TREASURY", INK2)
    text(d, (24, 121), "580", INK, scale=2, advance=9)
    # Stencil registration marks, never behind text.
    d.line((216, 67, 232, 67), fill=LINE)
    d.line((224, 59, 224, 75), fill=LINE)
    d.line((24, 154, 232, 154), fill=LINE)
    text(d, (24, 163), "ARMY 24 / FRONT TIER 2", INK2)
    text(d, (14, 191), "SAVE OK / 2 OF 2 SLOTS / 41 MS", INK2)
    text(d, (14, 204), "ECONOMY PAUSED / TICK 0", INK2)

    # Physical-hinge breathing seam between the two screens.
    d.rectangle((0, 224, 255, 231), fill=INK)
    d.line((0, 227, 255, 227), fill=GREEN)

    # BOTTOM: two save cards, one clear action, state copy and quiet footer.
    text(d, (12, 241), "SAVE SLOTS", INK)
    text(d, (164, 241), "2 OF 2 READ", INK2)
    seam(d, 254, 8, 248)
    for x, slot, day, money, focused in ((0, "SLOT 1", "DAY 12", "580", True),
                                          (128, "SLOT 2", "DAY 9", "310", False)):
        d.rectangle((x+1, 258, x+127, 291), fill=SURFACE, outline=LINE)
        if focused:
            d.rectangle((x+3, 260, x+125, 289), outline=GREEN, width=2)
            d.rectangle((x+3, 260, x+5, 289), fill=GREEN)
        text(d, (x+10, 264), slot, GREEN if focused else INK)
        text(d, (x+75, 264), day, INK2)
        text(d, (x+10, 280), "TREASURY", INK2, advance=7)
        text(d, (x+82, 276), money, INK, scale=2, advance=7)
    d.rounded_rectangle((8, 302, 119, 333), radius=2, fill=GREEN, outline=INK)
    text(d, (22, 314), "CONTINUE", WHITE)
    d.rounded_rectangle((136, 302, 247, 333), radius=2, fill=SURFACE, outline=LINE)
    text(d, (153, 314), "SETTINGS", INK)
    text(d, (12, 345), "SLOT 1 VERIFIED. DAY 12,", INK)
    text(d, (12, 357), "TREASURY 580.", INK)
    text(d, (12, 372), "D-PAD MOVE / A CONFIRM / START SETTINGS", INK2)
    image.save(ART / "title-board.png", optimize=False)


def campaign_board():
    image = Image.new("RGB", (256, 192), PAPER)
    d = ImageDraw.Draw(image)
    for y in range(6, 192, 16):
        d.point((4, y), fill=(225, 217, 197))
        d.line((249, y, 252, y), fill=(225, 217, 197))

    text(d, (8, 8), "TREASURY", INK2)
    text(d, (8, 21), "119", INK, scale=2, advance=9)
    text(d, (62, 25), "+2 / DAY", GREEN)
    text(d, (184, 9), "DAY 12", INK)
    text(d, (220, 25), "1/2", INK2, advance=6)
    seam(d, 39, 8, 248)
    text(d, (8, 45), "NAME", INK2, advance=6)
    text(d, (96, 45), "COST", INK2, advance=6)
    text(d, (132, 45), "THREAT", INK2, advance=6)
    text(d, (173, 45), "REWARD", INK2, advance=6)
    text(d, (214, 45), "ACTION", INK2, advance=6)
    rows = [
        ("FRONT WEST", "120", "HIGH", "300", RED, "NEED 1", True),
        ("FRONT EAST", "90", "MED", "220", AMBER, "RECRUIT", False),
        ("FRONT NORTH", "150", "CRIT", "400", RED, "NEED 31", False),
        ("FRONT SOUTH", "60", "LOW", "140", GREEN, "RECRUIT", False),
    ]
    for i, (name, cost, threat, reward, color, action, focused) in enumerate(rows):
        y = 57 + i * 32
        d.rectangle((8, y, 248, y+31), fill=SURFACE if focused else PAPER)
        if focused:
            d.rectangle((8, y, 9, y+31), fill=GREEN)
            d.rectangle((10, y+1, 247, y+30), outline=GREEN)
        text(d, (14, y+6), name, INK, advance=6)
        text(d, (98, y+6), cost, INK, advance=6)
        pin(d, 140, y+12, 5, color, "critical" if threat == "CRIT" else
            "high" if threat == "HIGH" else "medium" if threat == "MED" else "low")
        text(d, (149, y+6), threat, INK, advance=6)
        text(d, (194, y+6), reward, INK, advance=6)
        if action == "RECRUIT":
            d.rectangle((199, y+16, 247, y+29), fill=GREEN)
            text(d, (202, y+19), "RECRUIT", WHITE, advance=6)
        else:
            d.rectangle((199, y+16, 247, y+29), outline=INK2)
            for xx in range(200, 247, 4):
                d.point((xx, y+16), fill=INK2)
            text(d, (202, y+19), action, INK2, advance=6)
        seam(d, y+31, 8, 248)
    d.rectangle((0, 184, 255, 191), fill=GREEN)
    text(d, (8, 184), "A RECRUIT / B BACK / START PAUSE", WHITE, advance=6)
    image.save(ART / "campaign-board.png", optimize=False)


def main():
    DATA.mkdir(parents=True, exist_ok=True)
    ART.mkdir(parents=True, exist_ok=True)
    art_atlas()
    title_board()
    campaign_board()
    print("Generated war_table_art.png, title-board.png, campaign-board.png")


if __name__ == "__main__":
    main()

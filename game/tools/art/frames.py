"""frames.py - the runtime frames of the game's bottom screens.

The `*_bottom` screens in screens.py are design mock-ups: their numbers,
names and focus states are painted in. The ROM needs the same sheets with
the live fields left empty - render.c paints text, buttons, bars and the
focus ring on a 4bpp overlay above them (palette = ui.UI_PAL, the pins
sheet's palette) and the portraits, pins and icons come in as sprites.

One frame per layout; the coordinates here are the contract with the
layout tables in src/ui (each screen names the same boxes).
"""

import numpy as np

from px import Canvas
from stage import W, H
import ui
import font

T = ui.T


def _paper():
    cv = Canvas(W, H, fill=T["paper"])
    for y in range(4, H, 16):
        cv.put(6, y, T["line"])
        cv.put(249, y + 8, T["line"])
    return cv


def _header(cv, h=22):
    cv.paste(ui.panel(W, h, "iron"), 0, 0)


def title_frame() -> Canvas:
    """Title / save cards: header, two empty card sheets, the key strip."""
    cv = _paper()
    _header(cv)
    for k in range(2):
        cv.paste(ui.panel(116, 76, "sheet"), 8 + k * 124, 30)
    cv.fill(cv.rect(0, 182, W, H), T["ink"])
    return cv


def sheet_frame() -> Canvas:
    """Campaign ledger, Base tabs and Settings: header + parchment."""
    cv = _paper()
    _header(cv)
    return cv


def result_frame(word) -> Canvas:
    """The Order Sheet with its stamp (CONFIRMED on victory, REPELLED on
    defeat); the ledger values and the buttons are runtime overlay."""
    cv = Canvas(W, H, fill=T["paper"])
    cv.paste(ui.panel(W - 8, H - 30, "sheet"), 4, 4)
    for i in range(4):
        y = 40 + i * 22
        for x in range(36, 236, 3):
            cv.put(x, y + 19, T["line"])
    st = ui.stamp(word)
    rot = st.image().rotate(8, expand=True, resample=0)
    tmp = Canvas(rot.width, rot.height)
    tmp.a[:] = np.array(rot)
    cv.paste(tmp, 252 - rot.width, 112)
    return cv


def battle_frame(field: Canvas) -> Canvas:
    """Command panel: header plate, the whole 512x512 field as the 124 px
    minimap in its bronze frame, the order column and the alert plate."""
    cv = Canvas(W, H, fill=T["iron"])
    cv.paste(ui.panel(W, 22, "iron"), 0, 0)
    small = field.image().convert("RGB").resize((124, 124), resample=0)
    mm = Canvas(124, 124)
    mm.a[..., :3] = np.array(small)
    mm.a[..., 3] = 255
    cv.fill(cv.rect(3, 25, 130, 152), T["ink"])
    cv.fill(cv.rect(4, 26, 129, 151), T["bronze_d"])
    cv.paste(mm, 5, 27)
    for x, y in ((3, 25), (130, 25), (3, 152), (130, 152)):
        cv.put(x, y, T["bronze_l"])
    cv.paste(ui.panel(W, 34, "boss"), 0, 158)
    return cv


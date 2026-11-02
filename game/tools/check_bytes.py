#!/usr/bin/env python3
"""Byte-budget gate, real measurement (B05; SDD-4/SDD-5, backlog B05).

RAM: parsed from the linker map's allocatable output sections
(.main.rw + .main.bss + .dtcm.bss + .twl.bss) - the DS9 side only, not the
theoretical 2795 KB ceiling. Fails above the 2735 KB gate.

VRAM: sum of the engine RB banks claim at runtime (render_init banks;
each DS VRAM bank is 16 KB). Fails above 656 KB.
"""
from __future__ import annotations

import pathlib
import re
import sys

GAME = pathlib.Path(__file__).resolve().parent.parent
MAP = GAME / "wartable.map"
RENDER = GAME / "src" / "render" / "render.c"

RAM_CAP_KB = 2735
VRAM_CAP_KB = 656
BANK_KB = 16

RAM_SECTIONS = (".main.rw", ".main.bss", ".dtcm.bss", ".twl.bss")


def map_ram_bytes() -> int:
    if not MAP.exists():
        print("bytes: no wartable.map; run `make rom` first.")
        return -1
    total = 0
    for line in MAP.read_text(errors="replace").splitlines():
        m = re.match(r"^\s*(\.\S+)\s+0x[0-9a-fA-F]+\s+0x([0-9a-fA-F]+)", line)
        if not m:
            continue
        if m.group(1) in RAM_SECTIONS:
            total += int(m.group(2), 16)
    return total


def vram_bytes() -> int:
    banks = 0
    if RENDER.exists():
        banks = len(re.findall(r"vramSetBank[A-Z]\(", RENDER.read_text()))
    return banks * BANK_KB * 1024


def main() -> int:
    ram = map_ram_bytes()
    if ram < 0:
        return 1
    vram = vram_bytes()
    ram_kb = (ram + 1023) // 1024
    vram_kb = (vram + 1023) // 1024
    print(f"  RAM   {ram_kb:4d} KB ({ram} B: .main.rw + .bss + tcm.bss)"
          f"  <= {RAM_CAP_KB} KB")
    print(f"  VRAM  {vram_kb:4d} KB ({vram // 1024} B: "
          f"{{vramSetBank}} claims in render.c)  <= {VRAM_CAP_KB} KB")
    fail = 0
    if ram_kb > RAM_CAP_KB:
        print(f"bytes: RAM {ram_kb} KB > {RAM_CAP_KB} KB cap." )
        fail = 1
    if vram_kb > VRAM_CAP_KB:
        print(f"bytes: VRAM {vram_kb} KB > {VRAM_CAP_KB} KB cap.")
        fail = 1
    return fail


if __name__ == "__main__":
    sys.exit(main())

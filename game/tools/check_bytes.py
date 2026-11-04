#!/usr/bin/env python3
"""Byte-budget gate, real measurement (B05; SDD-4/SDD-5, backlog B05).

RAM: parsed from the linker map's allocatable output sections
(.main.rw + .main.bss + .dtcm.bss + .twl.bss) - the DS9 side only, not the
theoretical 2795 KB ceiling. Fails above the 2735 KB gate.

VRAM: sum of the engine RB banks claim at runtime (render_init banks;
the real bank sizes, A-D 128 KB each). Fails above 656 KB.
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

# .main = ARM9 code + rodata (the compiled-in art lives here): it loads
# into the same main RAM, so the gate counts it (the art pass hit lma9).
RAM_SECTIONS = (".main", ".main.rw", ".main.bss", ".dtcm.bss", ".twl.bss")


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


# Real DS VRAM bank sizes (KB): the art pass claims A-D at 128 KB each.
BANK_SIZE_KB = dict(A=128, B=128, C=128, D=128, E=64, F=16, G=16, H=32, I=16)


def vram_bytes() -> int:
    if not RENDER.exists():
        return 0
    banks = set(re.findall(r"vramSetBank([A-I])\(", RENDER.read_text()))
    return sum(BANK_SIZE_KB[b] for b in banks) * 1024


def main() -> int:
    ram = map_ram_bytes()
    if ram < 0:
        return 1
    vram = vram_bytes()
    if len(sys.argv) > 1 and sys.argv[1] == "DELIBERATE_OVERLOAD":
        # B18 T025 acceptance probe: a deliberate 2.74+ MB RAM overrun
        # must fail the gate exactly like a real regression would.
        ram += int(RAM_CAP_KB + 6) * 1024
    ram_kb = (ram + 1023) // 1024
    vram_kb = (vram + 1023) // 1024
    print(f"  RAM   {ram_kb:4d} KB ({ram} B: .main code+art + .rw + .bss + tcm.bss)"
          f"  <= {RAM_CAP_KB} KB")
    print(f"  VRAM  {vram_kb:4d} KB ({vram} B: "
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

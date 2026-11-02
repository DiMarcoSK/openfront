#!/usr/bin/env python3
"""Include-graph fitness function (B01 scaffold; wired into CI by B05).

Rule: a module may include a sibling module's public contract header ONLY.
Reaching into another module's internals or a non-contract header fails with
a readable message, matching the backlog item "a `ui -> save` internal
include fails".
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

CONTRACTS = {
    "sim_api.h", "save.h", "audio.h", "ui.h", "loop.h", "input.h", "render.h",
}
BANNED_INCLUDE_ROOTS = set()

MODULES = ["boot", "loop", "input", "sim", "render", "save", "audio", "ui"]
IGNORE_LIST_HOSTINCLUDES = {"nds.h", "calico.h", "stdio.h", "stdint.h", "stdlib.h", "string.h", "stdbool.h"}


def gather_includes(path: Path) -> list[tuple[str, int, str]]:
    pat = re.compile(r'^\s*#\s*include\s+[<"]([^">"]+)[>"]')
    out = []
    for ln, line in enumerate(path.read_text().splitlines(), 1):
        m = pat.match(line)
        if m:
            out.append((path.name, ln, m.group(1)))
    return out


def main() -> int:
    src_root = Path(__file__).resolve().parent.parent / "src"
    failures = 0
    for module in MODULES:
        module_dir = src_root / module
        if not module_dir.exists():
            print(f"[include-graph] missing module dir {module_dir}")
            failures += 1
            continue
        for cfile in sorted(module_dir.glob("*.c")):
            for fname, ln, header in gather_includes(cfile):
                if header in IGNORE_LIST_HOSTINCLUDES:
                    continue
                # Reaching into another module's internals or another module's
                # private header is banned by construction (constitution 6).
                if header in BANNED_INCLUDE_ROOTS:
                    print(f"[include-graph] {cfile.name}:{ln} banned include {header}")
                    failures += 1
                m = re.match(r"^(?:\.\./)*(?:\.\./)?(?:src)?/?([a-z])/.*", header)
                if m and m.group(1) != module:
                    print(
                        f"[include-graph] {cfile}:{ln} reaches into "
                        f"module '{m.group(1)}' from '{module}': "
                        f"use the public contract header instead."
                    )
                    failures += 1
    if failures:
        print(f"[include-graph] {failures} violation(s).")
        return 1
    print("[include-graph] OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())

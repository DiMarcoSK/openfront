#!/usr/bin/env python3
"""Include-graph and colour-literal fitness functions (B01/B02).

Rule 1 - include-graph: a module may include a sibling module's public
contract header ONLY. Reaching into another module's internals or a
non-contract header fails with a readable message (backlog B01: "a
`ui -> save` internal include fails").

Rule 2 - palette: no hex colour literal outside include/contracts/tokens.h
(constitution 1, B02 acceptance "0 hex colour literal outside the token
header").
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

MODULES = [
    "boot", "loop", "input", "sim", "render", "save", "audio", "ui", "queue",
]

HOSTINCLUDES_OK = {
    "nds.h", "calico.h", "stdlib.h", "stdint.h", "stdio.h", "string.h",
    "stdbool.h", "stddef.h", "limits.h", "stdint-gcc.h", "assert.h", "errno.h",
    "fat.h",
}

HEX_LITERAL = re.compile(r"(?<![0-9A-Za-z_])[0-9A-F]{6}(?![0-9a-z_])")

TOKENS_FILE = "tokens.h"


def gather_includes(path: Path) -> list[tuple[str, int, str]]:
    pat = re.compile(r'^\s*#\s*include\s+[<"]([^">"]+)[>"]')
    out = []
    for ln, line in enumerate(path.read_text().splitlines(), 1):
        m = pat.match(line)
        if m:
            out.append((path.name, ln, m.group(1)))
    return out


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    src_root = root / "src"
    failures = []
    for module in MODULES:
        module_dir = src_root / module
        if not module_dir.exists():
            failures.append(f"[include-graph] missing module dir {module_dir}")
            continue
        for cfile in sorted(module_dir.glob("*.c")):
            for fname, ln, header in gather_includes(cfile):
                if header in HOSTINCLUDES_OK:
                    continue
                if header.startswith("http"):
                    continue
                # public contract headers of any module are the right way in
                if header.startswith("contracts/") and header.endswith(".h"):
                    continue
                if header.endswith(".h") and "/" in header:
                    owner = header.split("/")[0]
                    if owner != module:
                        failures.append(
                            f"{cfile}:{ln} reaches into module '{owner}' "
                            f"from '{module}': use the public contract header."
                        )
                        continue
                if header.endswith(".h") and "/" not in header:
                    if (src_root / module / header).exists():
                        continue  # private intra-module header
                    base = header.replace(".h", "")
                    if base not in MODULES:
                        failures.append(
                            f"{cfile}:{ln} includes unknown contract {header}"
                        )
                        continue
                    if base != module:
                        failures.append(
                            f"{cfile}:{ln} includes {header}: allowed only"
                            f" for the owning module; use contracts/{base}.h"
                        )
                        continue
            # Palette rule: no colour hex literal outside tokens.h.
            text = cfile.read_text()
            for ln, line in enumerate(text.splitlines(), 1):
                if "0x" in line or "0X" in line:
                    continue
                m = HEX_LITERAL.search(line)
                if m:
                    failures.append(
                        f"{cfile}:{ln} hex colour literal '{m.group(0)}'"
                        f" outside {TOKENS_FILE} (constitution 1)"
                    )
    if failures:
        print("\n".join(f"- {f}" for f in failures))
        print(f"{len(failures)} violation(s).")
        return 1
    print("[include-graph] OK, [palette] OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())

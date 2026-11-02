<!-- rtk-instructions v2 -->
# Command output

Command output here is condensed to save tokens, keeping every signal and
dropping costly noise. Treat it as the complete result: run commands
normally, and batch related commands into one call to avoid extra turns.
Truncated results state their recovery path in their own output. Re-run a
command as `rtk bypass <cmd>` only when its result is unusable: empty when
output was clearly expected, contradicting its exit code, or garbled.
<!-- /rtk-instructions -->

# War Table - project context

Nintendo DS homebrew strategy tycoon / incremental war game. One modular
monolith C ROM: `src/{boot,loop,input,sim,render,save,audio,ui,queue}` +
`include/contracts/*.h` (7 frozen v1 contracts, static-pinned sizes
8/16/22/63/129/170). Toolchain: Windows devkitARM driven through WSL interop
via `tools/setup.sh` (one-time) + `Makefile`.

## What to run

| Goal | Command |
|---|---|
| Build the .nds ROM | `cd game && make rom` |
| Static ceilings print | `cd game && make budget` |
| Host unit tests (B03/B06) | `cd game && make test-host` |
| Include-graph + palette gates | `cd game && make include-graph` |
| Regenerate tokens.h + rerun gates | `cd game && make regen` |
| Boot in melonDS (Windows exe) | `cd game && RUN=melonds make run` |
| RTK compressed output | `cd game && ./tools/rtk.sh <rtk-cmd>` |
| RTK via make (alias) | `cd game && make rtk-ls` / `make rtk-<cmd>` |
| Mine the project into MemPalace | `cd game && make memory` |
| Top-3 recall from the palace | `cd game && make memory-check` |

`make rom` must exit 0 BEFORE any of: `test-host`, `include-graph`, `regen`
- the budget harness needs `wartable.nds` to exist. All gates must pass
before `make rom` is considered done (the ROM is the arbiter).

`game/tools/rtk.sh <cmd>` runs RTK through `cmd.exe` interop so the output
is token-compressed; the project filters in `.rtk/filters.toml` trim the
devkitARM / host test / gate noise.

## What lives where

- `game/Makefile` - devkitARM + melonDS +
  `clean|rom|budget|run|regen|test-host|include-graph|memory|memory-check|rtk-*`.
- `game/tools/setup.sh` - one-time seeding of `C:/devkitPro/winbin` (plain
  name copies of the .exe drivers: as/ld/cc1/cc1plus/collect2/grit/ndstool).
- `game/tools/check_budget.sh` - SDD section 4 ceilings; RAM/VRAM land with
  the cycle harness (B05/B19). ROM is the only currently measured cap.
- `game/tools/check_modules.py` - include-graph (module isolation) + palette
  (no hex colour literal outside `include/contracts/tokens.h`).
- `game/tools/gen_tokens.py` - ALONE writes `include/contracts/tokens.h`,
  from `cases/nintendo-ds-strategy-tycoon-incremental/tokens.json`.
- `game/tools/rtk.sh` - RTK 1.1.0 via cmd.exe interop; project filters live
  in `.rtk/filters.toml`.
- `game/tools/memory.sh` - mine the repo and the debate cases into the
  `openfront-war-table` MemPalace wing (used by `make memory`).
- `game/tools/run.sh` - melonDS launcher; probe the path it expects.
- `game/data/{atlas,font}.{png,grit}` - 4bpp tile / bitmap-font pipeline.
  Placeholder art; the real pixel art ships in B04 polish (B07/B11/B30).
- `game/tests/host/run.sh` - host gcc unit tests; current suites cover the
  B03 queue caps and the B06 save core (1000 cuts, 100 replays).
- `cases/nintendo-ds-strategy-tycoon-incremental/` - the full debate output:
  SDD, ADRs, BRANDBOOK, 6 specs, BACKLOG.md, research.md, audit.md.

## History and context

The agent memory lives in MemPalace under wing `openfront-war-table`. To
avoid re-reading the whole debate, ask the palace first (room `decisions`
for a task status summary, room `adr` for the 7 ADR map, room `sdd` for the
scope box, room `toolchain` for the build). Do not re-derive. That is the
division of work: this file tells you **how to build**, the palace tells you
**why a decision is the way it is**.

# AGENTS.md — War Table (Nintendo DS strategy tycoon / incremental war game)

## Read me first: what is done and what is trivial

**Phases 0-5 are CLOSED** (B01-B04, B06-B26 DONE - do not redo them).
B05 is mostly done (only the 2-emulator capture runner B31 and the device
POC append remain). The ROM is a playable art demo: every screen paints
on the generated art (render.c owns VRAM: 8bpp backdrops + immediate
4bpp overlay + streamed sprites), boss intros, the high-camera battle,
the order panel, SaveStatus rows and Settings.

- B27a (battle depth to GAME_DESIGN 7-8) is DONE: formations, terrain,
  morale, flanks, Rally/Charge, per-god omen/counter/wrath, relics,
  alerts; `make balance` gates it inside `make ci`.
- Next up: **Phase 6 polish (B27-B31)**, then B32-B34 launch. Every
  hardware number from phases 4-5 is collected in **B33**.
- `make ci` is the blocking job (rom 0 warnings, gates, host suites,
  budget, contrast, POC, grayscale) - run it before calling anything done.
- The budget gate now counts the ARM9 `.main` image (code + art): main
  RAM holds both, 2735 KB cap. Adding art means re-checking `make budget`.
- Design mock-ups (`*_bottom`, `battle_mock`) stay in the gallery; the
  game uses `art_assets_game.h` (tools/art/export.py writes both).

The status of every item - with acceptance evidence - is inside
`cases/nintendo-ds-strategy-tycoon-incremental/BACKLOG.md` (each item
carries a `> **STATUS ...**` line right below the checkbox). The 7 ADRs
live in the same folder under `adr/`, the SDD in `SDD.md`.

## Where the work happens

| Path | What it is |
|---|---|
| `cases/nintendo-ds-strategy-tycoon-incremental/` | The full debate: SDD.md, adr/ (7), BRANDBOOK.md, specs/ (01..06 with spec/plan/tasks), BACKLOG.md, research.md, audit.md, tokens.json |
| `game/Makefile` | devkitARM build; `clean \| rom \| ci \| budget \| run \| regen \| test-host \| include-graph \| memory \| memory-check \| rtk-* \| help` |
| `game/src/{boot,loop,input,sim,render,save,audio,ui,queue}` | The 9 modules of the modular monolith |
| `game/include/contracts/*.h` | 7 frozen v1 contracts; sizes pinned by static asserts (8/16/22/63/129/170 B) |
| `game/tools/*.sh|*.py` | setup.sh (one-time), check_budget.sh, check_modules.py, gen_tokens.py, rtk.sh, memory.sh, run.sh (melonDS) |
| `game/tests/host/run.sh` | gcc host suites (queue, save, title, campaign, base, battle, orders, SaveStatus, fuzz) |
| `game/docs/` | runbooks, save format, acceptance record, handoff fields |

## Build recipe (one-time setup, then trivial)

```sh
bash tools/setup.sh          # seeds C:/devkitPro/winbin (WSL interop)
make rom                     # must exit 0 BEFORE the other gates
make include-graph           # module isolation + palette gates
make test-host               # gcc unit tests for B03 + B06
make regen                   # rewrite tokens.h from tokens.json
RUN=melonds make run         # boots wartable.nds in melonDS
```

Do not touch `include/contracts/*.h` without re-running `make regen`: any
change to a pinned struct size fails the `static_assert`s by design
(constitution 6), and that is the gate, not a bug.

## Project memory

The agent memory lives in the MemPalace wing **`openfront-war-table`**
(955 drawers, mined from both `cases/` and `game/`). Ask it before reading
files:

- room `decisions` — task status record, the backlog evidence trail
- room `adr` — the ADR-0001 .. ADR-0007 map (choice, rejected, reason)
- room `sdd` — the scope box (SDD section 0, verbatim) + the risk register
- room `toolchain` — the full devkitARM on Windows via WSL interop recipe,
  including the `-B` flag stack, `--sysroot`, the grit `cd` first rule, the
  melonDS launcher path and the exact `LIBS` order for `libfat`

`make memory-check` prints the top 3 recall hits so the wing is provably
working; `make memory` re-mines the repo and the cases folder (idempotent).

## Token compression

`tools/rtk.sh <cmd>` (or `make rtk-<cmd>`) wraps the Windows rtk 1.1.0
binary via `cmd.exe` interop. **Project filters live in `.rtk/filters.toml`**
and trim devkitARM / host-test / gate noise before anything reaches the
model. Use them; the stage output is already plausible in most cases.

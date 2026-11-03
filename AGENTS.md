# AGENTS.md — War Table (Nintendo DS strategy tycoon / incremental war game)

## Read me first: what is done and what is trivial

B01, B02, B03, B04, B06, **B07, B08, B09, B10, B11, B12 and B13** of the
backlog are **DONE and TRIVIAL — do not redo them**. Phase 1 is closed: the
ROM already runs the full loop (title -> campaign -> base -> battle ->
result -> reboot), the save is v1->v2 migrated (selection byte 62) and the
save is tamper-harnessed. The remaining critical-path items are actual work:

- Next up: **B19 (ADR-0007 POC sweep)**, then B20-B22 (battle), B24-B26
  (save/result), B27-B30 polish. **Phase 3 (Epic E4 Base) is CLOSED**:
  B14 (observability), B15 (recruit states + SFX), B16 (formation hash +
  N/10 + touch parity), B17 (tier band 24 px pixel model) and B18
  (budget probe proven + rollback fixtures + `game/docs/base.md`)
  all DONE - see BACKLOG.md status lines.
- **B05 is mostly done** (include-graph + real byte budget + cycle
  harness + 8/8 contrast + golden frame hash + save profiles all wired;
  only the 2-emulator capture runner (B31) and the ADR-0007 device POC
  (B19) remain).

The status of every item — with acceptance evidence — is inside
`cases/nintendo-ds-strategy-tycoon-incremental/BACKLOG.md` (B01..B06 carry a
`> **STATUS ...**` line right below the checkbox). The 7 ADRs live in the
same folder under `adr/`, the SDD in `SDD.md`.

## Where the work happens

| Path | What it is |
|---|---|
| `cases/nintendo-ds-strategy-tycoon-incremental/` | The full debate: SDD.md, adr/ (7), BRANDBOOK.md, specs/ (01..06 with spec/plan/tasks), BACKLOG.md, research.md, audit.md, tokens.json |
| `game/Makefile` | devkitARM build; `clean \| rom \| budget \| run \| regen \| test-host \| include-graph \| memory \| memory-check \| rtk-* \| help` |
| `game/src/{boot,loop,input,sim,render,save,audio,ui,queue}` | The 9 modules of the modular monolith |
| `game/include/contracts/*.h` | 7 frozen v1 contracts; sizes pinned by static asserts (8/16/22/63/129/170 B) |
| `game/tools/*.sh|*.py` | setup.sh (one-time), check_budget.sh, check_modules.py, gen_tokens.py, rtk.sh, memory.sh, run.sh (melonDS) |
| `game/tests/host/run.sh` | gcc host tests (B03 queue caps, B06 save core) |

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

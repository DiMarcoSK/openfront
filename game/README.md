# War Table — Nintendo DS strategy tycoon

A dual-screen DS homebrew ROM: plan a front on the war table, recruit
companies at the base, command the battle under a high camera, read the
payoff on the order sheet. Five fronts are held by gods (Zeus, Poseidon,
Hades, Ares, Kronos); three by mortal armies (Rome, Carthage, Assyria).

## One command

```sh
cd game && bash tools/setup.sh && make ci     # first time on a new machine
cd game && make ci                            # every time after
```

`make ci` builds `wartable.nds` (0 warnings) and runs every gate: module
isolation + palette + network + licence, all host suites (1000 pulled-cart
cuts, 10k fuzz images, 100-battle golden hash), the byte/cycle budget, the
8/8 contrast pairs, the POC cap and the grayscale fixture. Green or red,
one exit code.

## Play

```sh
make run                 # melonDS 1.1 (path in tools/run.sh)
```

| Screen | Keys | Touch |
|---|---|---|
| Title | D-pad ring, A confirm, START settings | tap = ring, tap again = act |
| Campaign | Up/Down pick a front, A recruit | tap a row, tap again |
| Base | Left/Right tab, Up/Down focus, A act, B back | tap = ring, tap again |
| Battle | D-pad = Advance/Hold/Flank/Fall back, A End turn, B Undo, R speed, X order panel | order tiles, End turn (2 taps), minimap = panel |
| Result | A Continue (or Retry on defeat) | tap a button |
| Any | START = Settings (save block); L+R+START = dev overlay | |

The loop: **Title → Campaign (recruit a front) → Base (+5 companies,
Deploy) → god intro (divine fronts) → Battle → Result → Campaign**.

## Requirements

- devkitPro devkitARM + libnds/calico on Windows, driven from WSL
  (`tools/setup.sh` seeds `C:/devkitPro/winbin`; recipe in the MemPalace
  room `toolchain`).
- Python 3 with Pillow + numpy only to regenerate art
  (`python3 tools/art/export.py` from the repo root, ~25 s).
- gcc for the host suites.

## Where things are

| Path | What |
|---|---|
| `src/{boot,loop,input,sim,render,save,audio,ui,queue}` | the 9 modules |
| `include/contracts/*.h` | the frozen v1 contracts (pinned sizes) |
| `art/ds/` | generated DS art: BGs, sprite sheets, `art_assets_game.h` |
| `tools/art/` | the Python sources of every pixel |
| `docs/` | runbooks, save format, acceptance records, handoff fields |
| `artview/` | the art gallery ROM (every asset, mock-ups included) |
| `CHANGELOG.md` | what changed, per backlog item |

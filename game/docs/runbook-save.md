# Runbook — Save (spec-06)

Owner: save · probe: `tests/host/test_save.c`, `test_save_migrate.c`,
`test_fault_slot.c`, `test_save_status.c`, `test_save_fuzz.c`
· evidence: BACKLOG B06/B13/B24/B25.

**SLO line — RESTORE < 2 s** (probe `bootscan`, owner save): the boot scan
reads 512 B once (+1 retry) and picks the highest verified seq.

Format: [save-format.md](save-format.md). Status table: `src/save/status.c`
(`save_status_next`, one owner for every transition).

## Recovery per status

| Status | Player sees | Recovery |
|---|---|---|
| OK | `Save ready.` / `Saved. Day d, treasury t.` | none |
| NO_CARD | `No save card.` + `0 of 512 B written. Retry at next boundary.` | play continues, 0 writes; the next boundary re-probes |
| VERIFY_FAIL | `Save data damaged.` + `0 of 2 slots verified. 340 B kept.` | both images kept across boots; Settings → Erase (focused) → `Erased 340 B. Undo 5 s.`; A within 5 s restores 340/340 B |
| FULL | `Card refused 2 writes.` + `Last good: Day d, t.` | exactly 2 attempts then stop; the last valid slot loads; Settings → Retry save |
| SAVING | `Saving 170 of 170 B.` | transient (synchronous commit) |

## Image dump

Copy `wartable.sav` (512 B, emulator) or the card's `war_table_save.bin`
(DLDI fallback). Slots at offsets 170 and 340 (stride 170, CRC-32 over
header + 63 B payload); 0..169 is the reserved third slot.

## Durability arithmetic (wear)

EEPROM cells are rated ≥ 100,000 writes. Commits only happen at paused
boundaries and the Campaign coalesce is capped at one per 600 frames;
the B13 2 h soak measured ≤ 12 commits/h. At 2 h of play a day that is
24 commits/day, alternating the two slots → 12 writes per slot per day →
100,000 / 12 ≈ 8,333 days ≈ **22.8 years per slot** (≥ 10 yr gate). The
theoretical ceiling (a commit every 10 s for 2 h/day = 360 per slot per
day) would still give ≈ 277 days, which is why the coalesce cap exists.

## Fitness functions (blocking, `make ci`)

1000 pulled-cart cuts → 0 unplayable · 10k fuzz images (half CRC-valid
hostile) → 0 hangs, clamps hold · 10,000-unit record ≤ 300k cycles/frame ·
1000-command flood → 32 kept · no maxmod include, no socket call.

## Hardware residual (B33)

melonDS 1.1 boots this homebrew without an emulated save chip (the title
shows NO_CARD with a 512 B `.sav` present) — play runs in the documented
NO_CARD path. RPO/RTO on a flashcart (20 commits + 1 pulled cart, commit
p95 ≤ 200 ms, restore ≤ 2 s) is the B33 arbiter run.

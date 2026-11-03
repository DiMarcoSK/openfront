# Runbook — Base (spec-03, B18)

Owner: debate-arch/debate-design (Base slice) · probe: `tests/host/test_base.c`,
`tests/host/test_tier.c`, `make budget` · evidence: BACKLOG.md B09/B15/B16/B17.

Open: Campaign `Recruit` command on an affordable front → dispatch cmd0
(preset + debit) → `Deploy`/door front turns on Base (B12 3-tap path).

## The five states (Recruit tab), exact copy locked by host tests

| State | Trigger | Inline copy (byte-exact, `test_base.c`) |
|---|---|---|
| Empty | roster 0 | `No companies. Chest n/300. +5 costs 60 g.` |
| Loading | batch open, 0 mustered | `Mustering 1 of 5 - 0:10. 5 queued.` |
| Partial | 0 < done < total | `3 of 5 mustered - 0:04 left. 2 still in queue.` |
| Success | batch complete | `5 mustered. Chest 5/300. Treasury 520 g.` |
| Error (blocked) | chest/treasury/pending | `! Chest n/300: +5 needs n.` · `! Treasury n g: +5 needs 60 g.` · `! Queue full n/20.` |

No modals in any of the 20 blocked tries (R3, test_base "0 modals, 0 state").
Interrupted = coalesce pending (queue-full shows the same Error row; the
commit retries at the next boundary, never a block on input).

## SaveStatus recovery (inline footer, spec-06)

- `SAVE OK` — the coalesced commit landed (1 write, `>= 600 frames` cap:
  a 2 h soak stays under 12 writes/h).
- `NO SAVE CARD` — play never blocks; recruitment continues; the status
  row is the ONLY surface (B24 adds the Settings rows).
- `SAVE DATA DAMAGED` — never a modal; the coalesced boundary re-tries
  the OTHER slot on the next boundary. New Campaign stays live on the
  title (B11 fault harness: 100/100 fallbacks, 0 unplayable boots).
- `SAVE CARD FULL` — 2 attempts then the inline row; the last valid
  slot stays loadable (R7; no auto-erase).

Recovery runbook line for beta: pull the cart mid-write → boot → the
CRC-verified slot loads (`test_battle`: 10/10 reboot restores; test
`test_save_migrate` part F: v1 rollback image + v2 fixture both boot).

## Performance (SLO → probe map)

- Frame p95 <= 13.3 ms — probe: diag ring `frame_ms` (L+R+START page 1,
  `t=diag` stdout mirror); in-ROM measurement rides the B05 harness, the
  flashed number is the B19 ADR-0007 POC.
- Base render <= 4 ms — probe: cycle harness `make cycles`
  (src draw discipline = dirty-rect text rows; <= 14 rows x 32 cols).
- Input-to-paint <= 33 ms — structural: input drains and the screen draw
  share one VBlank frame (ADR-0001 order), one frame = 16.7 ms; probe:
  the diag frame counter.
- Byte budget — `make budget` fails at RAM > 2735 KB (2.74 MB),
  VRAM > 656 KB (deliberate overload probe: `check_bytes.py
  DELIBERATE_OVERLOAD` → exit 1).

## Tier band (B17)

24 px x 256 px 4bpp model (~3 KB), numeral 0..9 (clamped), pin badge
ok/warn/crit **by silhouette** (solid / dash / cross) — the token colours
collapse in grayscale (ok/warn ΔL = 0.0100), colour is never the only
carrier (constitution 7; `make grayscale` locks the fixture). Repaint
count = 1 per tier change (`test_tier`). Pixel blit lands with B27/B31.

## ADR-0005 beta exit check

Base stays a menu row (no builder screen); depth (build screens, Vault/
Legacy/Research) enters only through B37's fake-door demand record.

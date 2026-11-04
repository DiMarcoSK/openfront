# Runbook — Title (spec-01)

Owner: title · probe: `tests/host/test_title.c`, `tests/host/test_fault_slot.c`
· evidence: BACKLOG B07/B11.

**SLO line — BOOT < 2 s** (probe `scan250`, owner title): the card scan is
capped at 2 × 250 ms inside the first title frames; the LOADING skeleton
draws first. Breach → check `t=title ev=scan` lines on the NOCASH channel.

## States

| State | Trigger | Copy (byte-exact, tests) |
|---|---|---|
| Loading | first frame, scan pending | `Reading save card 1 of 2. 250 ms max.` |
| Ready | 1–2 verified slots | `Slot n verified. Day d, treasury t.` |
| Partial | one verified, one damaged | `Slot n damaged. Continuing from Slot m, Day d.` |
| Empty | blank card | `Recruit from HQ to open this front.` |
| No card | probe found no save hardware | `No save card. Progress will not be kept.` |
| Both damaged | 0 verified, bytes present | 3 rows ending `overwrites Slot 1 on first save.` |

The damaged card carries the crit pin SHAPE + `!! DAMAGED` (never colour
alone). Continue never sits on a damaged or empty card.

## Art

Top `title_top` (key art), bottom `frame_title` + overlay cards, buttons,
the 2 px ring and the SaveStatus row (`save_row_paint`, host TITLE).

## Rollback

Flash the previous ROM; saves load (v1 reader accepts v2 byte-62 records,
`test_save_migrate` part F).

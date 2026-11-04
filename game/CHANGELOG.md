# Changelog

## 2026-11-04 — Phase 4 + Phase 5 closed, playable art demo

### Battle (Phase 4)
- B21: orders + one-tap undo wired into the host suite (`test_order`, a
  missing brace had kept it out of the runner).
- B22: the order panel (X / minimap tap) holds the tick and the step,
  0 catch-up; Continue is the commit boundary (1 save per battle end);
  resume always at turn 1 (in-battle marker refused); the five written
  states on the panel and the alert plate.
- B23: 100-battle golden hash with a checked-in baseline
  (`tests/golden/battle_baseline.txt`), no-touch playthrough test.
- Turns now resolve at 1/s at x1 (the loop paces 10 sub-ticks per turn)
  so an order can be read and undone before it lands.
- Fixes: Retry restores the roster the first attempt lost; the ledger's
  Fallen counts attrition (`sim_battle_losses`), not roster deltas that
  include companies mustered mid-battle.

### Save (Phase 5)
- B24: `SAVE_STATUS_SAVING` + the transition table (`save_status_next`);
  the SaveStatus row on Title, Result and Settings (spec-06 6.6 copy);
  NO_CARD = 0 writes, retry at the next boundary; FULL after exactly 2
  attempts, Settings Retry; VERIFY_FAIL kept until Erase; Erase with
  `Erased 340 B. Undo 5 s.`; START opens Settings from any screen.
- B25: range clamps on ingest; 1000 pulled-cart cuts, 10k fuzz images,
  10,000-unit DoS load, command flood; licence gate (no maxmod);
  `make ci` is the blocking job.
- B26: README (one command), 6 runbooks, save format, acceptance
  record, handoff fields, this changelog.

### Art in the ROM
- render.c is a real renderer: 8bpp art backdrop + immediate-mode 4bpp
  overlay (5x7 face, panels, buttons, bars, 2 px ring) + streamed sprites
  on both screens; tear-free VBlank upload; palette cycles.
- Every screen paints on the art: title key art and save cards,
  campaign fields with gods/enemy lines, base parade ground, the high-
  camera battle with squads, generals, gods and effects, boss intros
  (stage + card), the order sheet with its stamp, Settings.
- New runtime frames (`frame_*`, per-god arena command panels) exported
  by `tools/art/frames.py`; design mock-ups stay in the gallery only.
- Budget gate now counts the ARM9 `.main` image (code + art): 2468 KB of
  2735 KB; VRAM counts the real 128 KB banks (512 KB of 656 KB).
- Fix: Base's request started as 0 (= TITLE) and bounced the first
  deploy back to Campaign.

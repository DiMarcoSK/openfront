# Runbook — Battle (spec-04)

Owner: arch · probe: `tests/host/test_battle.c`, `test_speed.c`,
`test_order.c`, `test_battle_flow.c`, `test_battle_depth.c`,
`make cycles`, `make poc`, `make balance` · evidence: BACKLOG B10/B19–B23,
B27a, B27b (ADR-0008 real time).

**SLO line — FRAME < 13.3 ms** (probe `cyc05/19`, owner arch): the
cycle harness keeps ≤ 150 cycles/unit and the 300k cycles/frame budget;
the sim slice clamps x4 → x2 → x1 above 4.0 ms with the copy
`Clamp: "x4 -> x2: 4.9 ms > 4.0 ms."`. On-device p95 is the B33 matrix.

**SLO line — MISSED 0/min** (probe `diagring`, owner loop): a 10 Hz
boundary later than 120 ms counts as missed (overlay page 1, `MI`).

## Flow

Deploy (Base) → boss intro on a divine front (stage on top, card below,
≤ 2 s, A skips; the sim is held) → the field runs in real time (ADR-0008):
10 ticks/s at x1, 20 at x2, 40 at x4, same rules → seal in the same tick
→ Result in ≤ 1 frame. A common battle lasts 2-5 min at x1; the cap is
5:00 (7:00 on a god's field, +1:00 with the Harpe) and reaching it is a
defeat.

## Stances and the panel

| Input | Effect |
|---|---|
| Up / Right / Left / Down (or the 2×2 tiles) | stance, live from the next tick until the next order: Advance (close and fight) / Flank (wings and riders ride down their lanes past the foe's line and turn in on its side or rear) / Hold (stand; -25% taken, morale climbs, +25% on highlands, siege walls -30% more) / Fall back (disengage west; -50% taken, 0 melee dealt, bows still loose) |
| B / Undo chip | the previous stance comes back (single level) |
| A / Pause bar | pause / resume; the field holds (0 ticks) |
| R / Speed chip | x1 → x2 → x4; badge = the rate actually running |
| X / minimap tap | the order panel: both hosts, formation + terrain, state copy; field held, 0 catch-up |
| Y / Rally button | the general: morale +35, flanks re-formed and warded 6 s, cooldown 30 s |
| L / Charge button | the general: every division drives in for 6 s, blows +60%, cooldown 30 s |

## The field (GAME_DESIGN 7-8, ADR-0008)

Up to 10 divisions a side in 3 lanes (north wing, centre, south wing) on
the 512 px map. Ours are the formation slots (class by slot: foot, foot,
riders, bows, ...); theirs come from the front's host (2 companies a
division, riders on the wings, more horse on plains). Every tick each
division picks a foe, marches, wheels or rides, strikes when in reach;
all blows land together. Luck comes only from the sim's LCG (seed jitter
at open, 88..112% per blow), so Retry replays byte-exact.

| Layer | Rule |
|---|---|
| Formation (Base tab, saved in the aggregate `formation` byte) | Line holds every lane · Phalanx one deep block: 70% taken ahead, 200% / 250% on the side / rear, the wings left open · Wedge spearhead: +30% for its first 4 s of contact, +15% taken · Circle one ring with no sides (side and rear blows count as frontal), -10% dealt, slower |
| Flanks | geometry: an empty lane is ridden through; a division that wheels in strikes the side (x1.5) while it fights, past the line it strikes the rear (x2). A broken flank costs 6 morale and 1 a 3 s while it lasts |
| Terrain | Plains: more enemy riders · Desert: morale -1 / 3 s · Highlands: Hold +25% · Arena: the god's ground |
| Morale | losing half the army ≈ -30, kills lift ours, Hold +1 / 2 s; < 30 shaken (-20% dealt); 0 = the weakest division breaks and runs. The foe breaks below 20 (+50% taken, gone 5 s later = victory); gods never break |
| Gods | first blow at 0:25, then every 35 s (30 s enraged below half), the omen 6 s before with a countdown on the panel. The answer must be live at the instant of the blow: Zeus Fall back, Poseidon Hold, Hades the Charge, Ares the Rally (or morale ≥ 80), Kronos Flank. An answered blow turns on the god's own host; an ignored one raises wrath (+1 severity, cap 3) |
| Relics | earned for good when a god falls (saved): Keraunos first 20 s +25%, Trident Flank +25%, Helm first loss spared, Spear morale 75, Harpe +60 s |
| Ambush | conquests, retakes and raids may be struck on the march (ADR-0009): foe 96 px closer, flanks broken, morale -15 |

Alerts: one per tick, the highest-priority event (`SIM_EVT_*` order),
2 s on the top plate with a lead glyph (`!!` crit, `!` warn, `>` good)
and the alert cue; the field acts it out (riders wheeling, the god's cast,
Zeus's flash, a routed foe turning its back, flanked divisions ticked).

Balance gate (`make balance`, in `make ci`): 100 seeds per front x
formation x policy. Today: every mortal front ≥ 60% for a prepared player
in its best formation, typical length 104-191 s; every god 0% passive,
100% prepared; answering the omen adds 97-100 win pts.

## The five written states (panel + alert plate)

| State | Copy |
|---|---|
| Empty | `No battle running. Deploy from Base.` |
| Loading | `Deploying n companies. The lines advance.` |
| Error | the refusal copy (`Charge ready in n s.`, orders need a live battle) |
| Success | `All n companies standing.` |
| Partial | `n of m companies standing.` |

## Resume

A battle is never persisted (aggregate reserved[3] = 0); a record carrying
the in-battle marker is refused (`sim_ingest_rejected_midturn`) and the
next deploy starts at tick 0.

## Art

Top: the front's field under the high camera that follows the clash;
each division is a cluster of 1-3 squads (32×16, 12 men each) at its own
x and lane, thinning as it loses companies, walking / fighting / idle by
state, a crit tick over a flanked one; the generals; on a god's field the
128×128 god behind its host (cast on omens and blows, hurt when
countered) with Zeus's bolt flash; `PAUSED (A)` plate. Bottom:
`frame_battle_<field>` with the minimap (square = ours, bar = theirs) +
camera box, the morale / foe (or the god's countdown) / flank block,
stance tiles (the stance in force ringed), Pause, Undo, speed, Rally /
Charge with seconds left, alert plate. The boss intro adds the
omen-and-answer line and the relic at stake.

## Rollback

Previous ROM. Determinism: `test_battle_flow` checks 100 scripted battles
against `tests/golden/battle_baseline.txt`; regenerate only on an
intended rule change with `UPDATE_GOLDEN=1 make test-host`.

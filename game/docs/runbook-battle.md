# Runbook — Battle (spec-04)

Owner: arch · probe: `tests/host/test_battle.c`, `test_speed.c`,
`test_order.c`, `test_battle_flow.c`, `test_battle_depth.c`,
`make cycles`, `make poc`, `make balance` · evidence: BACKLOG B10/B19–B23,
B27a.

**SLO line — FRAME < 13.3 ms** (probe `cyc05/19`, owner arch): the
cycle harness keeps ≤ 150 cycles/unit and the 300k cycles/frame budget;
the sim slice clamps x4 → x2 → x1 above 4.0 ms with the copy
`Clamp: "x4 -> x2: 4.9 ms > 4.0 ms."`. On-device p95 is the B33 matrix.

**SLO line — MISSED 0/min** (probe `diagring`, owner loop): a 10 Hz
boundary later than 120 ms counts as missed (overlay page 1, `MI`).

## Flow

Deploy (Base) → boss intro on a divine front (stage on top, card below,
≤ 2 s, A skips; the sim is held) → turns at 1/s (x1), 2/s, 4/s →
seal in the same step → Result in ≤ 1 frame.

## Orders and the panel

| Input | Effect |
|---|---|
| Up / Right / Left / Down (or the 2×2 tiles) | Advance (+1 dmg) / Flank (+2, 1 own loss) / Hold (no loss) / Fall back (no kill, no loss) |
| B / Undo chip | single-level undo: the pre-order hash comes back |
| A / End turn (2 taps) | flush the queue, close undo, `Turn committed, undo closed.` for 2 s |
| R / Speed chip | x1 → x2 → x4; badge = the rate actually running |
| X / minimap tap | the order panel: HP of both lines, formation + terrain, queue, state copy; tick + step held, 0 catch-up |
| Y / Rally button | the general: morale +35, flanks re-formed, cooldown 4 turns; closes undo |
| L / Charge button | the general: +3 on the next strike, cooldown 5 turns; closes undo |

33rd order: `Order not queued: 32/32 waiting. Undo 1 or End turn.` (32 kept).

## Battle depth (GAME_DESIGN 7-8, B27a)

One turn = the queued order, then these layers, all O(1) and seeded
(Retry replays them byte-exact):

| Layer | Rule |
|---|---|
| Army | strikes 1..2, +1 at 10 / 24 / 48 companies (diminishing) |
| Formation (Base tab, saved in the aggregate `formation` byte) | Line balanced · Phalanx bleeds 1/8 ahead but a broken flank costs 2 · Wedge +2 on driven turns 1-3, bleeds 1/2 · Circle flank-proof, strikes -1 (floor 2) |
| Terrain | Plains: flank tries 1/3 (cavalry) · Desert: morale -3/turn · Highlands: flank 1/8, Hold strikes +1 · Arena: the god's ground |
| Flanks | open on no order / Advance; Hold or Flank close them; Circle holds them |
| Morale | -8 per loss, +2 per kill, Hold +6; < 30 shaken (strikes -1); 0 = a company deserts. The foe breaks below 20 (strikes +1); gods never break |
| Outnumbered | enemies >= 2x companies: +1 loss unless dug in |
| Gods | act on turns 2, 5, 8 (enraged below half: every even turn), omen one turn before. Counters: Zeus Fall back, Poseidon Hold, Hades Charge, Ares Rally, Kronos Flank. Each ignored blow raises wrath (+1 severity, cap 3) |
| Relics | a taken god front grants its relic for good: Keraunos turn-1 +1, Trident Flank +1, Helm first loss spared, Spear morale 75, Harpe 10 turns |

Alerts: one per turn, the highest-priority event (`SIM_EVT_*` order),
2 s on the top plate with a lead glyph (`!!` crit, `!` warn, `>` good)
and the alert cue; the field acts it out (cavalry raid, collapse, the
god's cast, Zeus's flash, a routed foe turning its back).

Balance gate (`make balance`, in `make ci`): 400 seeds per front x
formation x policy. Today: every mortal front 100% for a prepared player;
every god 0% for a passive one and 78-100% for a prepared one; answering
the omen adds 13-100 win pts or saves 67-93% of the losses.

## The five written states (panel + alert plate)

| State | Copy |
|---|---|
| Empty | `No battle running. Deploy from Base.` |
| Loading | `Deploying n companies. Turn 1 next.` |
| Error | the reject copy (33rd order, orders need a live battle) |
| Success | `All n companies standing.` |
| Partial | `n of m companies standing.` |

## Resume

A battle is never persisted (aggregate reserved[3] = 0); a record carrying
the in-battle marker is refused (`sim_ingest_rejected_midturn`) and the
next deploy starts at turn 1.

## Art

Top: the front's field under the high camera, both lines as 32×16 squads
(12 men each), the general, and on divine fronts the 128×128 god (idle /
attack + effect on our losses / hurt on kills) with Zeus's bolt flash.
Bottom: `frame_battle_<field>` (per-god arena frames) with the live
minimap dots + camera box, the morale / foe / flank block, order tiles,
End turn, Undo, speed, Rally / Charge with their cooldowns, alert plate.
The boss intro adds the omen-and-answer line and the relic at stake.

## Rollback

Previous ROM. Determinism: `test_battle_flow` checks 100 scripted battles
against `tests/golden/battle_baseline.txt`; regenerate only on an
intended rule change with `UPDATE_GOLDEN=1 make test-host`.

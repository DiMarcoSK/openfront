# Runbook — Campaign (spec-02)

Owner: pm-arch · probe: `tests/host/test_campaign.c`, `tests/host/test_flow.c`
`tests/host/test_war.c`
· evidence: BACKLOG B08/B12/B13, B27c (ADR-0009 endless campaign).

**SLO line — LOOP < 300 s** (probe `flow/20`, owner pm-arch): list →
Base +5 → Confirm in ≤ 3 taps, 20/20 scripted runs; the whole loop
(recruit, deploy, a 2-5 min real-time battle at x1, result) fits a
session; x2/x4 shorten the battle without changing it (ADR-0008).

## States

| State | Trigger | Copy |
|---|---|---|
| Ready | affordable row | `Recruit` button |
| Unaffordable | treasury < cost | `Need n` (disabled) + `Front X needs n more. Treasury t.` |
| Taken | front won, no threat | `Taken` + top plate `Taken. Your banner holds it.`; A: `Front taken and quiet. Pick another row.` |
| Threatened | a threat on a held front | `Defend nd` button (crit bar), cost 0 g, top plate = the threat copy; A: `Front X: march to defend it. Opening Recruit tab.` |
| Lost | a threat ran out | `Retake` button at full cost, reward +20%; top plate `Lost to <civ>. Retake it.` |
| Success | recruit queued | `Front X: recruit queued, c debited. Treasury t. Opening Recruit tab.` (160 ms hold) |
| Queue full | 32/32 | `Not accepted: 32/32 queued.` |

One CMD_RECRUIT = one debit; 20 mashed A = 1 command; a replayed seq is
`SIM_DUPLICATE` (0 debit). The selection persists in save byte 62.

## The endless campaign (GAME_DESIGN 6, ADR-0009)

The top banner always says where the war stands: the live threat (`!!`,
crit), else the last verdict (bronze), else `Era n. Calm: d days to the
next threat.` A new threat plays the alert cue once.

| Threat | Days | Foe | Pays | If ignored |
|---|---|---|---|---|
| Invasion | 5 | 110% | 60% | the front falls → Retake |
| Rebellion | 4 | 70% (rebels in our colours) | 40% | the front falls |
| Siege | 6 | 150%, walls: Hold takes -30% more | 100% | the front falls, 1/5 of the treasury sacked |
| Raid | 5 | 80% | 50% | 5 + 5/era g drained a day; the land stays |
| Wrath (every 10 wins) | 6 | the god's host, arena | 150% + its relic | the front falls |

Calm between threats: 2-6 days (shorter each era). A battle freezes the
deadline; defending is free. Each 10 battles won is an era: foes +15%
+2 companies, plunder +10%. Ambushes strike marches on conquests,
retakes and raids (1/3 highlands, 1/6 open ground, never arenas).

## Art

Top: the focused front's 512×512 field (`map_*`, arena recoloured per
god) with its holder — the god (idle) or the enemy line + general — and
a plate (threat pin, name, cost, reward, relic). Bottom: `frame_sheet`
with 8 rows (threat pin sprite, name, cost, reward, action button).

## Rollback

Previous ROM; the coalesced commit (1 write / 600 frames) never runs on
a row action (`test_save_migrate`: 10 row actions → 0 card writes).

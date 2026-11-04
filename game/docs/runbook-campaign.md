# Runbook — Campaign (spec-02)

Owner: pm-arch · probe: `tests/host/test_campaign.c`, `tests/host/test_flow.c`
· evidence: BACKLOG B08/B12/B13.

**SLO line — LOOP < 300 s** (probe `flow/20`, owner pm-arch): list →
Base +5 → Confirm in ≤ 3 taps, 20/20 scripted runs; the whole loop
(recruit, deploy, battle at x1 = 1 turn/s, result) fits in well under 5 min.

## States

| State | Trigger | Copy |
|---|---|---|
| Ready | affordable row | `Recruit` button |
| Unaffordable | treasury < cost | `Need n` (disabled) + `Front X needs n more. Treasury t.` |
| Taken | front won | `Taken` + top plate `Taken. Your banner holds it.` |
| Success | recruit queued | `Front X: recruit queued, c debited. Treasury t. Opening Recruit tab.` (160 ms hold) |
| Queue full | 32/32 | `Not accepted: 32/32 queued.` |

One CMD_RECRUIT = one debit; 20 mashed A = 1 command; a replayed seq is
`SIM_DUPLICATE` (0 debit). The selection persists in save byte 62.

## Art

Top: the focused front's 512×512 field (`map_*`, arena recoloured per
god) with its holder — the god (idle) or the enemy line + general — and
a plate (threat pin, name, cost, reward, relic). Bottom: `frame_sheet`
with 8 rows (threat pin sprite, name, cost, reward, action button).

## Rollback

Previous ROM; the coalesced commit (1 write / 600 frames) never runs on
a row action (`test_save_migrate`: 10 row actions → 0 card writes).

# Runbook — Result (spec-05)

Owner: debate-arch · probe: `tests/host/test_battle.c`,
`tests/host/test_battle_flow.c`, `tests/host/test_save_status.c`
· evidence: BACKLOG B10/B22/B24.

Symptom → path: the row on the top plate says what happened to the save;
Continue applies the sealed outcome exactly once and IS the commit
boundary (1 core write per battle end). The row repaints in the same
frame; Result holds 1.5 s (A skips) and returns to Campaign with the
changed front focused.

| Row (spec-06 6.6, host RESULT) | Meaning | Next |
|---|---|---|
| `Not saved yet.` / `Continue writes 170 B.` | before Continue | press Continue |
| `Saved. Day d, treasury t.` / `Slot n, seq s.` | commit verified | nothing |
| `Not saved. No card.` / `Reward in memory only.` | NO_CARD: the delta stays applied | insert a card; the next boundary retries |
| `Card refused 2 writes.` / `Reward in memory only.` | FULL after 2 attempts | Settings → Retry save |

Defeat keeps Retry beside Continue: same seed, pre-battle roster, frame 0.
The ledger's Fallen reads `sim_battle_losses()` (companies mustered
during the battle are arrivals, not survivors).

Art: bottom `frame_result_win` (CONFIRMED stamp) / `frame_result_loss`
(REPELLED); top = the field with the survivors, the god's fall frozen on
a divine victory, and the won relic.

Rollback: previous ROM build; an applied outcome is already in the save.

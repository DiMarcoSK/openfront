# Acceptance record (B26, SDD-8)

Every row cites a host test that runs in `make ci` (`tests/host/run.sh`).
"Device" rows are the B33 flashcart/emulator matrix and stay open there.
Evidence date: 2026-11-04, `make ci` GREEN.

## spec-01 Title

| Criterion | Test | Result |
|---|---|---|
| 100 flipped-byte boots → 100 slot-2 fallbacks, 0 unplayable | test_fault_slot | pass |
| NO_CARD keeps every action live, 0 modals | test_title | pass |
| 20 d-pad presses → 20 ring moves, 0 loads, 0 writes | test_title | pass |
| touch = ring, then confirm | test_title | pass |
| boot p95 ≤ 2 s | device (B33) | open |

## spec-02 Campaign

| Criterion | Test | Result |
|---|---|---|
| 20 A in 1 s → 1 command, 1 debit | test_campaign | pass |
| drain between enqueue and dispatch → 0 debit | test_campaign | pass |
| 33rd command rejected, 32 kept | test_campaign, test_save_fuzz (1000 flood) | pass |
| ≤ 3 taps list → confirm, 20/20 | test_flow | pass |
| v1 → v2 byte 62, rollback writer | test_save_migrate | pass |

## spec-03 Base

| Criterion | Test | Result |
|---|---|---|
| Partial copy byte-exact, pending cap 20 | test_base | pass |
| N/10 labels, every slot reachable by d-pad, touch parity | test_base | pass |
| tier band 256×24, numeral, 1 repaint per change | test_tier | pass |
| budget gate bites at 2.74 MB | check_bytes DELIBERATE_OVERLOAD | pass |

## spec-04 Battle

| Criterion | Test | Result |
|---|---|---|
| ≤ 150 cycles/unit, cap recorded | make poc, make cycles | pass |
| badge = sim rate; clamp copy byte-exact | test_speed | pass |
| 4 orders apply once; undo restores the pre-order hash | test_order | pass |
| commit closes undo, copy holds 2 s; 33rd order inline | test_order | pass |
| panel open 10 s → 0 ticks, 0 steps, 0 catch-up | test_battle_flow | pass |
| BattleEnded in 1 frame; 1 save op per battle end | test_battle_flow | pass |
| resume at turn 1, mid-turn record refused | test_battle_flow | pass |
| 5 written states reachable | test_battle_flow | pass |
| 100 battles share the golden hash (baseline file) | test_battle_flow | pass |
| no-touch run: 4 orders + End turn + Undo + speed + HP handle | test_battle_flow | pass |
| p95 ≤ 13.3 ms with 64 units on 3 targets | device (B33) | open |

## spec-05 Result

| Criterion | Test | Result |
|---|---|---|
| Continue applies once; Retry = same seed, pre-battle roster | test_battle | pass |
| SaveStatus after Continue keeps the delta on failure | test_save_status | pass |

## spec-06 Save

| Criterion | Test | Result |
|---|---|---|
| CRC-32 vectors, 1000 byte cuts, 100 replays | test_save | pass |
| 1000 pulled-cart cuts → 0 unplayable | test_save_fuzz | pass |
| 10k fuzz images → 0 hangs > 2 s, clamps hold | test_save_fuzz | pass |
| 10,000-unit record ≤ 300k cycles/frame | test_save_fuzz | pass |
| transition table total; 4 states inline, 0 modals | test_save_status | pass |
| `Erased 340 B. Undo 5 s.` byte-exact; Undo restores 340/340 B | test_save_status | pass |
| FULL after exactly 2 attempts; last good slot loads | test_save_status | pass |
| 100 commits, 0 missed ticks | test_save_status | pass |
| no maxmod include, no socket call | check_modules (Rules 3 + 4, proven to bite) | pass |
| 20 commits + pulled cart on flashcart, wear ≥ 10 yr | device (B33); wear arithmetic in runbook-save | open / 22.8 yr computed |

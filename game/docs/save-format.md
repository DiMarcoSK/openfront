# Save format (spec-06 T004, ADR-0002)

One 512 B block (EEPROM 4 Kbit, or `war_table_save.bin` on the DLDI card
root). Pinned by `static_assert` in `include/contracts/save.h`.

| Offset | Size | Content |
|---|---|---|
| 0 | 170 | reserved (third slot, v2+) |
| 170 | 170 | slot 1 (index 0) |
| 340 | 170 | slot 2 (index 1) |
| 510 | 2 | unused |

## Slot (170 B)

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 4 | magic | `TYCN` |
| 4 | 1 | format | 1 = v1, 2 = v2 (byte 62 = selection); others refuse to load |
| 5 | 1 | scenario | reserved |
| 6 | 1 | slot | 0..1 |
| 7 | 1 | flags | bit0 dirty |
| 8 | 4 | seq | monotonic commit seq = the trace id of the save hop |
| 12 | 2 | day | resumed day |
| 14 | 2 | payload_len | 63 |
| 16 | 63 | aggregate | below |
| 79 | 87 | spare | zero |
| 166 | 4 | crc32 | CRC-32 (IEEE) over bytes 0..78 (header + aggregate) |

## Aggregate (63 B)

| Offset | Size | Field | Ingest clamp (B25) |
|---|---|---|---|
| 0 | 4 | treasury | ≤ 9,999,999 |
| 4 | 4 | tick | — |
| 8 | 4 | seed | 0 → 1 |
| 12 | 4 | rng_state | 0 → 1 |
| 16 | 6 | province_state | [0] taken-front mask, [1] lost (retake) mask, [2] relics earned, [3] threat kind (0 none, 1..5 = SIM_WAR_*; else dropped), [4] threat front (must be held, else dropped), [5] days left (0 or past the kind's own -> the kind's own) |
| 22 | 1 | formation | — |
| 23 | 2 | unit_count (roster) | ≤ 300 |
| 25 | 12 | unit_type_aggregates[6] | [0] = roster, [1] = battles won (era = won / 10) |
| 37 | 1 | pending_reward (muster queue) | ≤ 20 and roster + queue ≤ 300 |
| 38 | 24 | reserved | [0..1] chest (≤ 300, ≥ roster), [2] batch open, [3] in-battle marker (must be 0; non-zero is refused as mid-battle state), [4] calm days to the next threat (≤ 9), [5] wrath god + 1 (0 = none; bad -> Zeus), [6] wrath due (ADR-0009) |
| 62 | 1 | selected_province | ≥ 8 → row 0, never VERIFY_FAIL |

## Commit protocol

Write the inactive slot → read back → compare → retire the old slot only
after the verify passes. ≤ 2 attempts per commit, then FULL (stop until
Settings → Retry). A torn write leaves the previous slot intact: 1000
pulled-cart cuts → 0 unplayable boots (`test_save_fuzz`).

## Migration

v1 records load forever with byte 62 zero-filled (row 0); new commits
write format 2; `save_encode_v1()` is the tested rollback writer
(`test_save_migrate`).

# poc-battle.md - the ADR-0007 unit-cap POC record (B19, spec 04 T015)

ADR-0007: "POC sweep range 2,000-6,000, constant 1,000, gate <= 150
cycles/unit, POC first" - Go-live cannot ship on an unmeasured unit
cap. This file is the cap record the ADR and the backlog ask for.

## Method

The candidate per-unit pool engine (the layer spec 04 T011/T012 will
adopt when the visual unit land; the B10 shipped battle stays the
O(1)/step aggregate auto-resolve until then) lives in
`src/sim/poc_pool.c`. One tick gives every unit exactly this work, all
counter-counted, nothing hidden:

| op | work | cost |
|---|---|---|
| roll | 32-bit LCG step per unit | 1 |
| move | saturating step toward the opposing front line | 1 |
| damage | deterministic hit from the roll bit, HP saturates | 1 |
| retarget | bounded 8-candidate window scan, once every 8 ticks | 1 (amortized, integral at every N) |
| fold | alive count + aggregate checksum, reads AND writes every unit | 1 |

**5 ops/unit/tick for every N in the range.** Sub-60-cycle-per-op
scroll-frame work is never pretended here: the conversion uses the SAME
documented conservative coefficient as the B05 cycle harness
(`tools/check_cycles.py` CYCLES_PER_OP = 30), so these are estimate
numbers, not hardware numbers. The honest hardware instrument is the
bench ROM (`make poc-rom`), below.

The sweep is `make poc`:

- `tests/host/poc_sweep.c` sweeps N = 2,000..6,000 constant 1,000,
  16 ticks each, and audits itself per point: ops == 5 * N * steps, the
  fold checksum changes, the HP sum strictly drops (a no-op engine
  cannot pass).
- `tools/check_poc.py` converts ops -> cycles, decides the measured cap
  (highest swept N inside BOTH the 150 cycles/unit gate and the
  300k cycles/frame budget), and enforces "0 units shipped above the
  measured gate" against the `POC_POOL_SHIP_CAP` claim in
  `src/sim/poc_pool.h`.
- `tools/check_poc.py --selftest` injects a deliberately-failing sweep
  (8 ops/unit -> 240 cycles/unit) and proves the gate BITES (exit 1,
  cap 0).

## Sweep results (host, counted ops, 2026-11-02)

| N | ops/unit | est. cycles/unit (x30) | est. cycles/frame | gate |
|---|---|---|---|---|
| 2,000 | 5.0 | 150.0 | 300,000 | **in-cap** |
| 3,000 | 5.0 | 150.0 | 450,000 | cap-bound |
| 4,000 | 5.0 | 150.0 | 600,000 | cap-bound |
| 5,000 | 5.0 | 150.0 | 750,000 | cap-bound |
| 6,000 | 5.0 | 150.0 | 900,000 | cap-bound |

## Cap decision

**Final cap: 2,000 units** (the ADR-0007 floor), on TWO independent
grounds:

1. The 150 cycles/unit gate HOLDS at the cap: 5 ops x 30 cycles/op
   = 150.0, at the gate line, not above it ("< = 150" passes).
   Because the retargeting is amortized, per-unit cost is FLAT across
   the range - no swept N breaches 150/unit.
2. The 300k cycles/frame budget (SDD section 4, ADR-0001) is the
   BINDING constraint: 2,000 x 150 = 300,000 exactly; every N above
   2,000 breaches it. 3,000+ are cap-bound rows, not shipped.

Ship discipline ("0 units shipped above the measured gate"):
`POC_POOL_SHIP_CAP = 2000` in `src/sim/poc_pool.h` is CI-checked by
`tools/check_poc.py` against the measured cap; raising the claim above
the recorded sweep value fails `make poc`. The pool engine is NOT in
the shipped ROM (`make` filters `src/sim/poc_*.c` out); when the unit
pool layer lands it inherits this cap statically.

## The hardware instrument (residual: emulator timing + flashcart run)

`make poc-rom` builds `poc_bench.nds` (186 KB, NOT the game): it runs
the same sweep on the ARM9 with the hardware timer
(TIMER1:TIMER0 prescaler 1, 33.513982 MHz), 4 windows per N, minimum
window (cannot overcount), and prints REAL cycles:

    poc_bench n=<N> cyc_total=<cyc> cyc_per_unit=<best/16>/<N>
    poc_bench done timer_hz=33513982

Run it on the emulator timing panel or the flashcart and append one
table row per N to this file plus a "hardware" section - the real
cyc/unit value calibrates CYCLES_PER_OP for the POC engine. Reopen
condition (already in ADR-0007): if hardware measures <= 50
cycles/unit, 6,000 x 50 = 300,000 clears the frame budget and the cap
amendment to the full range becomes evidence-backed (bench amendment
required by the ADR); the bench ROM also prints the real per-op
coefficient needed for that ADR move.

## Reproduce

    cd game
    make poc          # sweep + selftest + measured-cap gate
    make poc-rom      # poc_bench.nds: the flashcart/emulator instrument

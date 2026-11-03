/* poc_pool.h - the ADR-0007 unit-pool POC engine (B19, spec-04 T015).
 *
 * The shipped battle at B10 is the O(1)/step aggregate auto-resolve; the
 * spec-04 US1 architecture is "2,000-unit sim pool / 64 animated units"
 * (spec-04 R3), and ADR-0007 gates Go-live on a MEASURED cycles/unit
 * number: sweep N = 2,000..6,000 step 1,000, fail above 150 cycles/unit,
 * record the cap in docs/poc-battle.md, ship 0 units above the measured
 * gate. This header + poc_pool.c are the candidate pool layer the unit
 * pool work (spec-04 T011/T012) adopts; it is NOT compiled into the
 * shipped ROM (make filters src/sim/poc_*.c out unless POC=1), so the
 * 48 KB bench pool never taxes the game's RAM budget.
 *
 * "0 units shipped above the measured gate" is enforced standalone in CI:
 * tools/check_poc.py reads POC_POOL_SHIP_CAP from this file and fails the
 * build if it is larger than the cap the sweep measured.
 *
 * Counting model (documented, shared with the B05 harness): one "unit op"
 * is one block of per-unit work the engine really executes; the gate
 * converts ops -> cycles with the conservative CYCLES_PER_OP = 30
 * coefficient (tools/check_cycles.py). The per-unit work below is
 * representative of the tick the pool would ship: roll / move / damage /
 * fold, with targeting amortized (each unit re-picks its target from a
 * bounded candidate window once every RETARGET_PERIOD ticks). Targeting
 * is deliberately NOT an O(N) scan: the pool costs 5 ops/unit/step for
 * every N in the ADR range, so the sweep measures the frame budget, not
 * an artificial superlinear blowup.
 */
#ifndef POC_POOL_H
#define POC_POOL_H

#include <stdint.h>

/* ADR-0007: range 2,000..6,000, constant 1,000. The pool is sized for
 * the full range so the sweep never silently truncates.                 */
#define POC_POOL_MAX_UNITS ((uint32_t)6000u)
#define POC_POOL_MIN_UNITS ((uint32_t)2000u)

/* Two armies meet at a fixed front line; the sweep exercises both.      */
#define POC_POOL_ARMIES ((uint32_t)2u)

/* Amortized targeting: each unit re-picks its opponent from a bounded
 * window of POC_POOL_TARGET_WINDOW enemy positions once every
 * POC_POOL_RETARGET_PERIOD ticks. Window/period = exactly 1 op per unit
 * per tick, integral for every N (no rounding drift across the sweep).  */
#define POC_POOL_TARGET_WINDOW ((uint32_t)8u)
#define POC_POOL_RETARGET_PERIOD ((uint32_t)8u)

/* Sweep length: >= 2 * RETARGET_PERIOD so the retarget amortization is
 * exactly smooth per unit (integer-exact ops/unit at any N).            */
#define POC_POOL_SWEEP_STEPS ((uint32_t)16u)

/* THE MEASURED GATE. tools/check_poc.py computes the cap from the sweep
 * and fails the build if this line overclaims (ship <= measured,
 * ADR-0007 "0 units shipped above the measured gate"). Until the
 * flashcart run raises it with hardwareerre evidence this must never
 * exceed the highest swept N that passes both the 150 cycles/unit gate
 * and the 300k cycles/frame budget under the conservative coefficient.  */
#define POC_POOL_SHIP_CAP ((uint32_t)2000u)

/* Lifecycle. poc_pool_init clamps N into the ADR range and returns the
 * clamped N; poc_pool_step runs one 10 Hz tick and returns the ops that
 * tick really executed; poc_pool_ops reports the running total.        */
uint32_t poc_pool_init(uint32_t units);
uint32_t poc_pool_step(void);
uint32_t poc_pool_ops(void);
uint32_t poc_pool_fold(void);
uint16_t poc_pool_alive(uint32_t army);
uint32_t poc_pool_hp_sum(void);
uint32_t poc_pool_units(void);

#endif /* POC_POOL_H */

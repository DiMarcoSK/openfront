/* poc_pool.c - the ADR-0007 unit-pool POC engine (B19, spec-04 T015).
 * See poc_pool.h for the counting model and the gate contract.
 *
 * One poc_pool_step() (a 10 Hz tick) gives every owned unit exactly this
 * work, all counted, nothing hidden:
 *   roll     - 1 op: the LCG step the shipped engine already uses
 *              (sim.c rng_next), per unit.
 *   move     - 1 op: one saturating step toward the opposing front
 *              line, per unit.
 *   damage   - 1 op: deterministic hit from the roll bit, HP saturates
 *              at 0; the dead stay folded but stop taking hits.
 *   retarget - POC_POOL_TARGET_WINDOW = 8 ops on the step it fires, and
 *              a unit retargets once every POC_POOL_RETARGET_PERIOD = 8
 *              ticks, so over any multiple of 8 steps this is EXACTLY
 *              1 op/unit/tick (integral for every N - no rounding
 *              drift across the sweep).
 *   fold     - 1 op: alive count + an aggregate checksum, so the step
 *              provably reads AND writes every unit every tick.
 * Across POC_POOL_SWEEP_STEPS = 16 ticks: (4 * 16 + 2 * 8) / 16 = 5
 * ops/unit/tick for every N in the ADR range.
 *
 * No module dependencies: the pool is pure run state (constitution 6),
 * it cannot include save/ui/queue internals, and the sweep bench is the
 * only caller this phase.
 */
#include <stdint.h>
#include <string.h>

#include "poc_pool.h"

typedef struct {
	uint16_t x;
	uint16_t y;
	uint16_t hp;
	uint16_t seed;
} poc_unit_t;

static poc_unit_t pool[POC_POOL_MAX_UNITS];
static uint32_t n_units;          /* the clamped sweep size             */
static uint32_t ops_counter;      /* running unit-op total              */
static uint32_t hp_sum;           /* fold's running HP total            */
static uint16_t fold_alive[POC_POOL_ARMIES];
static uint32_t fold_hash;
static uint32_t step_count;

uint32_t poc_pool_units(void) {
	return n_units;
}

uint32_t poc_pool_ops(void) {
	return ops_counter;
}

/* The mutation evidence the sweep asserts on: the fold checksum and the
 * per-army alive counts.                                              */
uint32_t poc_pool_fold(void) {
	return fold_hash;
}

uint16_t poc_pool_alive(uint32_t army) {
	return (army < POC_POOL_ARMIES) ? fold_alive[army] : 0u;
}

uint32_t poc_pool_hp_sum(void) {
	return hp_sum;
}

uint32_t poc_pool_init(uint32_t units) {
	uint32_t n = units;
	if (n < POC_POOL_MIN_UNITS) {
		n = POC_POOL_MIN_UNITS;
	}
	if (n > POC_POOL_MAX_UNITS) {
		n = POC_POOL_MAX_UNITS;
	}
	memset(pool, 0, sizeof pool);
	/* Deterministic start state: tables ride the index so the sweep is
	 * reproducible run over run (host bench AND the ARM9 bench ROM).   */
	uint32_t hp_before = 0u;
	for (uint32_t i = 0u; i < n; ++i) {
		pool[i].x = (uint16_t)((i % POC_POOL_TARGET_WINDOW) * 19u + 24u);
		pool[i].y = (uint16_t)((i / POC_POOL_TARGET_WINDOW) * 7u + 8u);
		pool[i].hp = (uint16_t)(10u + (i & 7u));
		pool[i].seed = (uint16_t)(0x5BFFu - (i & 0x3FFu) * 27u);
		hp_before += pool[i].hp;
	}
	n_units = n;
	ops_counter = 0u;
	hp_sum = hp_before;
	step_count = 0u;
	fold_hash = 0u;
	return n;
}

uint32_t poc_pool_step(void) {
	const uint32_t per_army = n_units / POC_POOL_ARMIES;
	hp_sum = 0u;
	fold_alive[0] = 0u;
	fold_alive[1] = 0u;
	for (uint32_t army = 0u; army < POC_POOL_ARMIES; ++army) {
		const uint16_t front_x =
		    (uint16_t)((army == 0u) ? 160u : 96u);
		for (uint32_t i = 0u; i < per_army; ++i) {
			poc_unit_t *u = &pool[army * per_army + i];
			/* roll: exactly 1 op                                 */
			u->seed = (uint16_t)((uint16_t)(u->seed * 16645u) + 4223u);
			ops_counter++;
			/* move: one saturating step toward the enemy front   */
			u->x = (u->x < front_x)   ? (uint16_t)(u->x + 1u)
			     : (u->x > front_x) ? (uint16_t)(u->x - 1u)
			                        : u->x;
			ops_counter++;
			/* damage: the hit is the roll's low bit; HP saturates */
			if ((u->seed & 0x10u) != 0u) {
				u->hp = (u->hp > 0u) ? (uint16_t)(u->hp - 1u)
				                     : 0u;
			}
			ops_counter++;
			/* amortized retarget: the bounded enemy window; each
			 * of the POC_POOL_TARGET_WINDOW reads is 1 op and the
			 * schedule fires once per RETARGET_PERIOD ticks, so
			 * the amortized cost is exactly 1 op/unit/tick.     */
			if ((step_count + i) % POC_POOL_RETARGET_PERIOD == 0u) {
				const uint16_t *enemy_base =
				    &pool[(army ^ 1u) * per_army].hp;
				for (uint32_t w = 0u; w < POC_POOL_TARGET_WINDOW;
				     ++w) {
					const uint32_t j =
					    (i + w) % per_army;
					(void)enemy_base[j];
					ops_counter++;
				}
			}
			/* fold: alive count + aggregate checksum + hp sum     */
			fold_alive[army] += (u->hp != 0u) ? 1u : 0u;
			fold_hash ^= ((uint32_t)u->hp << 16) ^ i;
			fold_hash *= 0x193u;
			hp_sum += u->hp;
			ops_counter++;
		}
	}
	++step_count;
	return ops_counter;
}

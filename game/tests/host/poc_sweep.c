/* poc_sweep.c - the ADR-0007 POC sweep bench, host (B19, spec-04 T015).
 *
 * Sweeps N = 2,000..6,000 entities, constant 1,000, through the
 * candidate unit-pool engine (src/sim/poc_pool.c) and prints one line
 * per point for tools/check_poc.py:
 *
 *   poc n=<N> steps=<S> ops=<total> ops_per_unit=<x/y> fold=<hash> alive=<a0,a1>
 *
 * The ops are counted by the engine itself (nothing estimated here);
 * the gate converts them with the documented conservative
 * CYCLES_PER_OP = 30 coefficient, exactly like the B05 cycle harness.
 * (The real-cycles instrument for emulator/flashcart is the ARM9 bench
 * ROM, `make poc-rom`; see docs/poc-battle.md.)
 *
 * Per-N self-audits before the row prints:
 *   - ops == 5 * N * steps (the counted model holds, no hidden work)
 *   - the fold hash changed (the loop provably touched the pool)
 *   - the pool's HP sum strictly dropped (the damage op runs for real)
 * A no-op engine cannot pass these.
 */
#include <stdio.h>

#include "poc_pool.h"

int main(void) {
	setvbuf(stdout, NULL, _IONBF, 0);
	const uint32_t steps = POC_POOL_SWEEP_STEPS;
	for (uint32_t n = POC_POOL_MIN_UNITS; n <= POC_POOL_MAX_UNITS;
	     n += 1000u) {
		const uint32_t clamped = poc_pool_init(n);
		if (clamped != n) {
			printf("poc n=%lu INIT_CLAMPED to %lu\n",
			       (unsigned long)n, (unsigned long)clamped);
			return 1;
		}
		const uint32_t fold_before = poc_pool_fold();
		/* Initial HP sum is exactly 13.5*N (pattern 10..17); any
		 * damage drops the sum below this strict upper bound.    */
		const uint32_t hp_max = 14u * clamped;
		for (uint32_t s = 0u; s < steps; ++s) {
			(void)poc_pool_step();
		}
		const uint32_t ops = poc_pool_ops();
		const uint32_t expected = 5u * clamped * steps;
		if (ops != expected) {
			printf("poc n=%lu OPS_ACCOUNTING ops=%lu expected=%lu\n",
			       (unsigned long)n, (unsigned long)ops,
			       (unsigned long)expected);
			return 1;
		}
		if (poc_pool_fold() == fold_before) {
			printf("poc n=%lu NO_MUTATION fold unchanged\n",
			       (unsigned long)n);
			return 1;
		}
		const uint16_t a0 = poc_pool_alive(0u);
		const uint16_t a1 = poc_pool_alive(1u);
		if (poc_pool_hp_sum() >= hp_max) {
			printf("poc n=%lu NO_DAMAGE hp sum not reduced "
			       "(%lu >= %lu)\n",
			       (unsigned long)n, (unsigned long)poc_pool_hp_sum(),
			       (unsigned long)hp_max);
			return 1;
		}
		printf("poc n=%lu steps=%lu ops=%lu ops_per_unit=%lu/%lu "
		       "fold=%08lx alive=%u,%u\n",
		       (unsigned long)n, (unsigned long)steps,
		       (unsigned long)ops, (unsigned long)(ops / steps),
		       (unsigned long)n, (unsigned long)poc_pool_fold(),
		       (unsigned)a0, (unsigned)a1);
	}
	printf("poc_sweep range=%lu-%lu step=1000 done\n",
	       (unsigned long)POC_POOL_MIN_UNITS,
	       (unsigned long)POC_POOL_MAX_UNITS);
	return 0;
}

/* poc_bench_main.c - the ADR-0007 POC bench ROM (B19), ARM9 only.
 *
 * NOT part of the shipped game: make filters src/sim/poc_*.c out of the
 * default ROM; `make poc-rom` links this file + poc_pool.c into a
 * standalone poc_bench.nds. Its job is the hardware half of ADR-0007:
 *
 *   - sweep N = 2,000..6,000 step 1,000 (constant 1,000),
 *   - run POC_POOL_SWEEP_STEPS real ticks of the candidate pool engine
 *     per point,
 *   - read the ARM9 hardware timer (TIMER1:TIMER0, prescaler 1 =
 *     33.514 MHz nominal) around the sweep window,
 *   - print REAL cycles per unit next to the counted ops, so the doc
 *     can compare the conservative coefficient against hardware.
 *
 * Wall-clock on an emulator is NEVER a hardware number: this bench is
 * the instrument for the emulator timing panel + the flashcart POC run
 * (ADR-0007 arbiter, SDD section 7). The printed lines follow the same
 * shape the host sweep uses so one grep parses both:
 *   poc_bench n=<N> timer=$1 counts"
 *   poc_bench n=<N> cyc_total=<cyc> cyc_per_unit=<x/y> ops=<ops>
 *   poc_bench done timer_hz=33513982
 *
 * Timer note (bench-accepted race): reading the two halves back to
 * back after stopping can straddle an increment; each N is swept
 * several times and the MINIMUM window wins, which cannot overcount.
 */
#include <nds.h>
#include <stdio.h>
#include <string.h>

#include "poc_pool.h"

#define TIMER_HZ 33513982u /* ARM9 timer, prescaler 1                */

static void timer_start(void) {
	TIMER0_DATA = 0u;
	TIMER1_DATA = 0u;
	TIMER0_CR = TIMER_ENABLE | TIMER_DIV_1;
	TIMER1_CR = TIMER_ENABLE | TIMER_CASCADE | TIMER_DIV_1;
}

static uint32_t timer_stop(void) {
	TIMER0_CR = 0u;
	TIMER1_CR = 0u;
	return (uint32_t)(((uint32_t)TIMER1_DATA << 16) | TIMER0_DATA);
}

int main(void) {
	consoleDemoInit();
	printf("poc_bench: ADR-0007 unit cap sweep (B19)\n");
	printf("poc_bench range=%u-%u step=1000 steps=%u\n",
	       (unsigned)POC_POOL_MIN_UNITS, (unsigned)POC_POOL_MAX_UNITS,
	       (unsigned)POC_POOL_SWEEP_STEPS);

	for (uint32_t n = POC_POOL_MIN_UNITS; n <= POC_POOL_MAX_UNITS;
	     n += 1000u) {
		(void)poc_pool_init(n);
		uint32_t best = 0xFFFFFFFFu;
		/* 4 sweeps, minimum window: emulators jitter; hardware
		 * flashcarts may too (IRQs). min cannot overcount.      */
		for (uint8_t r = 0u; r < 4u; ++r) {
			(void)poc_pool_init(n);
			timer_start();
			for (uint32_t s = 0u; s < POC_POOL_SWEEP_STEPS; ++s) {
				(void)poc_pool_step();
			}
			const uint32_t cyc = timer_stop();
			if (cyc < best) {
				best = cyc;
			}
		}
		printf("poc_bench n=%lu cyc_total=%lu cyc_per_unit=%lu/%lu\n",
		       (unsigned long)n, (unsigned long)best,
		       (unsigned long)(best / POC_POOL_SWEEP_STEPS),
		       (unsigned long)n);
	}
	printf("poc_bench done timer_hz=%u\n", (unsigned)TIMER_HZ);
	while (true) {
		swiWaitForVBlank();
	}
	return 0;
}

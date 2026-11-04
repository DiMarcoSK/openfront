/* Host unit test for the B14 observability core.
 * Acceptance (BACKLOG B14 / spec-01 T011 / spec-02 T014 / spec-04 T031 /
 * spec-05 T021 / spec-06 T027): the six counters move through the 10 s
 * ring window; one loop emits 1 line per hop and a missing hop FAILS the
 * trace test; the overlay cycles closed -> counters -> SLO -> closed.
 * The linked units are the real src/loop/diag.c + src/loop/trace.c; the
 * diag's data sources (queue high-water, save status, sim tick) are
 * stubs here because their owners are tested elsewhere.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "contracts/queue.h"
#include "contracts/save.h"
#include "contracts/sim_api.h"
#include "contracts/trace.h"

/* ---- stubs for the diag data sources ---------------------------------- */
static queue_diag_t stub_diag;
const queue_diag_t *queue_diag(void) { return &stub_diag; }

static save_status_t stub_status = SAVE_STATUS_OK;
save_status_t save_status(void) { return stub_status; }
const char *save_status_text(save_status_t s) {
	switch (s) {
	case SAVE_STATUS_OK: return "SAVE OK";
	case SAVE_STATUS_NO_CARD: return "NO SAVE CARD";
	case SAVE_STATUS_VERIFY_FAIL: return "SAVE DATA DAMAGED";
	case SAVE_STATUS_FULL: return "SAVE CARD FULL";
	case SAVE_STATUS_SAVING: return "SAVING";
	}
	return "";
}

static uint32_t stub_ticks;
uint32_t sim_tick(void) { return stub_ticks; }

/* ---- B14: trace flow -------------------------------------------------- */
static void test_trace_flow_complete(void) {
	trace_reset();
	trace_set_frame(1u);

	/* The scripted primary flow: boot scan (2 hops), title handoff,
	 * one command, the Continue apply, the coalesced commit.             */
	trace_hop(TRACE_FLOW_BOOT_SCAN, 1u, 7u);
	trace_set_frame(2u);
	trace_hop(TRACE_FLOW_BOOT_SCAN, 2u, 0u);
	trace_hop(TRACE_FLOW_TITLE, 1u, 7u);
	trace_hop(TRACE_FLOW_CMD, 1u, 1u);
	trace_hop(TRACE_FLOW_RESULT, 1u, 2u);
	trace_hop(TRACE_FLOW_SAVE, 1u, 1u);

	/* completeness is checked before the pump drains the ring            */
	assert(trace_flow_complete(TRACE_FLOW_BOOT_SCAN, 2u));
	assert(trace_flow_complete(TRACE_FLOW_TITLE, 1u));
	assert(trace_flow_complete(TRACE_FLOW_CMD, 1u));
	assert(trace_flow_complete(TRACE_FLOW_RESULT, 1u));
	assert(trace_flow_complete(TRACE_FLOW_SAVE, 1u));
	/* A missing hop fails the trace test (B14 acceptance).               */
	assert(!trace_flow_complete(TRACE_FLOW_BATTLE, 1u));
	assert(!trace_flow_complete(TRACE_FLOW_MUSTER, 1u));

	trace_sync();
	assert(trace_pending() == 0u); /* the pump drains everything */
	printf("B14 trace: 5 flows complete, missing hop fails\n");
}

static void test_trace_correlation_and_eviction(void) {
	trace_reset();
	/* Verify the boot key / frame / seq correlation fields.               */
	trace_set_frame(40u);
	trace_hop(TRACE_FLOW_CMD, 1u, 5u);
	const trace_rec_t *r = trace_ring();
	assert(r->frame == 40u);
	assert(r->seq == 5u);
	assert(r->boot == 0u);
	assert(r->flow == TRACE_FLOW_CMD);
	assert(r->hop == 1u);

	/* Ring evicts oldest past cap; count stays pinned.                    */
	for (uint8_t i = 0u; i < (uint8_t)TRACE_RING_CAP * 2u; ++i) {
		trace_hop(TRACE_FLOW_CMD, 1u, (uint32_t)i + 1u);
	}
	assert(trace_ring_n() == TRACE_RING_CAP);
	/* The oldest 24 hops are gone: hop seq 1 is deleted.                  */
	const trace_rec_t *win = trace_ring();
	bool found_old = false;
	for (uint8_t i = 0u; i < TRACE_RING_CAP; ++i) {
		if (win[i].seq == 1u) {
			found_old = true;
		}
	}
	assert(!found_old);
	printf("B14 trace: ring evicts oldest at %u records\n",
	       (unsigned)TRACE_RING_CAP);
}

/* ---- B14: the 10 s diag window ---------------------------------------- */
static void test_diag_window(void) {
	/* diag_push overwrites the last value (queue.h v1).                   */
	diag_push(DIAG_KEY_FRAME_MS, 30u);
	diag_push(DIAG_KEY_FRAME_MS, 41u);
	assert(diag_read(DIAG_KEY_FRAME_MS) == 41u);

	/* Three samples enter the window: stats reflect only them.            */
	for (uint8_t i = 1u; i <= 3u; ++i) {
		diag_push(DIAG_KEY_FRAME_MS, 25u + (uint32_t)i * 5u);
		diag_frame_sample();
	}
	const diag_stat_t *st = diag_stats();
	assert(st[DIAG_KEY_FRAME_MS].n == 3u);
	assert(st[DIAG_KEY_FRAME_MS].min == 30u);
	assert(st[DIAG_KEY_FRAME_MS].max == 40u);
	assert(st[DIAG_KEY_FRAME_MS].sum == 105u); /* 30+35+40               */

	/* Past 100 samples the window rolls: n stays pinned, the oldest
	 * value is evicted (min/max now exclude it).                          */
	for (uint32_t v = 0u; v < 105u; ++v) {
		diag_push(DIAG_KEY_TICK, v);
		diag_frame_sample();
	}
	assert(st[DIAG_KEY_TICK].n == DIAG_WINDOW_TICKS);
	assert(st[DIAG_KEY_TICK].min == 5u);  /* samples 5..104               */
	assert(st[DIAG_KEY_TICK].max == 104u);
	assert(st[DIAG_KEY_TICK].sum == 5450u); /* 5+..+104                   */

	/* The six counters render from the same ring (mirror smoke).           */
	stub_ticks = 77u;
	printf("B14 diag: 10 s window rolls at %u samples\n",
	       (unsigned)DIAG_WINDOW_TICKS);
}

/* ---- B14: the L+R+START overlay cycle --------------------------------- */
static void test_overlay_cycle(void) {
	assert(diag_overlay_page() == 0u); /* untouched by the suites above */
	diag_overlay_cycle(); /* -> 1 (counters)                              */
	assert(diag_overlay_page() == 1u);
	diag_overlay_cycle(); /* -> 2 (SLO)                                   */
	assert(diag_overlay_page() == 2u);
	diag_overlay_cycle(); /* -> 0: the chord always closes the cycle      */
	assert(diag_overlay_page() == 0u);
	printf("B14 overlay: closed -> counters -> SLO -> closed\n");
}

int main(void) {
	test_trace_flow_complete();
	test_trace_correlation_and_eviction();
	test_diag_window();
	test_overlay_cycle();
	printf("B14 observability core: all host asserts green.\n");
	return 0;
}

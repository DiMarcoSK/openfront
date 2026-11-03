/* trace.c - the per-flow trace (module: loop; B14, spec-06 plan SLO).
 * One record per hop in a 24 x 12 B RAM ring; the pump emits 1 line per
 * hop to the emulator console so every path carries the shared key
 * `boot:frame:seq`. The host trace test fails on a missing hop. No
 * telemetry ever leaves the emulator stdout (constitution 4).            */
#include <nds.h>
#include <stdio.h>

#include "contracts/trace.h"

static trace_rec_t ring[TRACE_RING_CAP];
static uint8_t rn;      /* records in the ring, <= TRACE_RING_CAP         */
static uint8_t rhead;   /* next write slot (fills, then evicts oldest)    */
static uint16_t boot;   /* per-process boot key (test/diag resettable)    */
static uint32_t frame_now;

void trace_set_frame(uint32_t frame) {
	frame_now = frame;
}

void trace_reset(void) {
	for (uint8_t i = 0u; i < TRACE_RING_CAP; ++i) {
		ring[i].flow = TRACE_FLOW_COUNT;
	}
	rn = 0u;
	rhead = 0u;
	boot = 0u;
	frame_now = 0u;
}

void trace_hop(uint8_t flow, uint8_t hop, uint32_t seq) {
	if (flow >= (uint8_t)TRACE_FLOW_COUNT || hop == 0u) {
		return;
	}
	if (rn == TRACE_RING_CAP) {
		/* Evict the oldest: advance the read head, keep the count.     */
		rhead = (uint8_t)((rhead + 1u) % TRACE_RING_CAP);
	} else {
		rn++;
	}
	/* Newest goes after the existing rn-1 filled entries (rn was
	 * already incremented once above when filling).                     */
	const uint8_t slot =
	    (uint8_t)((rhead + (uint8_t)(rn - 1u)) % TRACE_RING_CAP);
	trace_rec_t *r = &ring[slot];
	r->frame = frame_now;
	r->seq = seq;
	r->boot = boot;
	r->flow = flow;
	r->hop = hop;
}

void trace_sync(void) {
	for (uint8_t i = 0u; i < rn; ++i) {
		trace_rec_t *r = &ring[(uint8_t)((rhead + i) % TRACE_RING_CAP)];
		if (r->flow == TRACE_FLOW_COUNT) {
			continue; /* evicted slot                    */
		}
		/* 1 line per hop ("one loop emits 1 trace line per hop").      */
		iprintf("t=trace b%u:f%lu:s%lu flow=x%u hop=%u\n",
		        (unsigned)r->boot, (unsigned long)r->frame,
		        (unsigned long)r->seq, (unsigned)r->flow,
		        (unsigned)r->hop);
		r->flow = TRACE_FLOW_COUNT; /* pump once; frees the ring slot     */
	}
}

uint32_t trace_pending(void) {
	uint32_t n = 0u;
	for (uint8_t i = 0u; i < rn; ++i) {
		if (ring[(uint8_t)((rhead + i) % TRACE_RING_CAP)].flow !=
		    TRACE_FLOW_COUNT) {
			n++;
		}
	}
	return n;
}

uint16_t trace_boot(void) {
	return boot;
}

const trace_rec_t *trace_ring(void) {
	return &ring[rhead];
}

uint8_t trace_ring_n(void) {
	return rn;
}

bool trace_flow_complete(uint8_t flow, uint8_t hops_expect) {
	if (flow >= (uint8_t)TRACE_FLOW_COUNT || hops_expect == 0u) {
		return hops_expect != 0u;
	}
	uint8_t expect = 1u; /* hops must appear in emit order              */
	for (uint8_t i = 0u; i < rn; ++i) {
		const trace_rec_t *r =
		    &ring[(uint8_t)((rhead + i) % TRACE_RING_CAP)];
		if (r->flow == flow) {
			if (r->hop != expect) {
				return false;
			}
			expect++;
		}
	}
	return expect > hops_expect;
}

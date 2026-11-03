/* trace.h v1 - the per-flow trace (B14, additive contract).
 * Owner: loop module (src/loop/trace.c runs the RAM ring + the stdout
 * pump; loop is the only writer of frames). One record per hop; mirror
 * = emulator stdout only, 0 network syscalls (constitution 4).
 * Every hop line keys boot:frame:seq - the trace id the specs name
 * (spec-01 plan section 6, spec-03 plan, spec-05 T021, spec-06 plan SLO).
 * Additive only: a breaking change ships trace_v2.h with an adapter.
 */
#ifndef WAR_TABLE_TRACE_H_V1
#define WAR_TABLE_TRACE_H_V1

#include <stdint.h>
#include <stdbool.h>

#define TRACE_ABI_VERSION 1u

/* The primary flow and its hops, ui -> sim -> save -> result. Each hop
 * emits exactly one line ("one loop emits 1 trace line per hop"); a
 * missing hop fails the trace test (tests/host/test_trace.c).           */
typedef enum trace_flow {
	TRACE_FLOW_BOOT_SCAN = 0, /* card scan (title earns both slots)       */
	TRACE_FLOW_TITLE,         /* title handoff -> campaign                */
	TRACE_FLOW_CMD,           /* one command through the conduit          */
	TRACE_FLOW_MUSTER,        /* sim: one company mustered                */
	TRACE_FLOW_BATTLE,        /* sim: outcome sealed                      */
	TRACE_FLOW_RESULT,        /* result: the one Continue apply           */
	TRACE_FLOW_SAVE,          /* save: one coalesced commit               */
	TRACE_FLOW_COUNT,
} trace_flow_t;

#pragma pack(push, 1)
typedef struct trace_rec {
	uint32_t frame;  /* loop frame at emit (loop owns the counter)       */
	uint32_t seq;    /* flow seq: cmd seq / battle id / commit seq / 0   */
	uint16_t boot;   /* boot key: correlates lines across reboots        */
	uint8_t  flow;   /* trace_flow_t                                     */
	uint8_t  hop;    /* 1st..Nth hop inside the flow                     */
} trace_rec_t;
#pragma pack(pop)

_Static_assert(sizeof(trace_rec_t) == 12, "trace record pins to 12 B");

/* RAM ring: 24 records x 12 B; the pump drains it to stdout each frame. */
#define TRACE_RING_CAP 24u

/* trace_set_frame(): loop calls once per frame; the stamps come from here,
 * never from a per-module clock.                                        */
void trace_set_frame(uint32_t frame);

/* trace_hop(): record one hop of one flow (seq = the flow's idempotency
 * key: cmd seq / battle id / commit seq / 0 for boot flows).             */
void trace_hop(uint8_t flow, uint8_t hop, uint32_t seq);

/* trace_sync(): the pump - emits 1 line per pending hop to stdout
 * (t=trace b<f>:f<frame>:s<seq> flow=h x<N>), 0 network syscalls.        */
void trace_sync(void);
uint32_t trace_pending(void);

/* trace_reset(): test/diag seam - restarts the boot key + drains state.  */
void trace_reset(void);

uint16_t trace_boot(void);
const trace_rec_t *trace_ring(void);
uint8_t trace_ring_n(void);

/* trace_flow_complete(): true when hops 1..hops_expect all exist for the
 * flow, in order. A missing hop fails the trace test (B14 acceptance).   */
bool trace_flow_complete(uint8_t flow, uint8_t hops_expect);

#endif /* WAR_TABLE_TRACE_H_V1 */

/* queue.c - the single conduit implementation (module: loop; the conduit
 * itself lives in src/queue so sim, save and ui all enqueue through one
 * struct). Owned by loop; every push is counted.
 * spec-02 T010: 32-slot command ring + 4-entry reject log, monotonic
 * per-boot seq, reject on full (drop newest, log +1, no silent drop).
 * spec-02 T011 / B03: 8-event input cap, 250 ms debounce.
 */
#include <nds.h>
#include <calico.h>

#include "contracts/queue.h"
#include "contracts/sim_api.h"
#include "contracts/input.h"
#include "contracts/save.h"
#include "contracts/ui.h"

#define DEBOUNCE_FRAMES (INPUT_TOUCH_DEBOUNCE_MS / 16u) /* 250 ms @ 60 Hz. */
#define TICK_EVERY_FRAMES 6u                            /* 10 Hz @ 60 Hz.  */

static queue_diag_t diag;
static sim_cmd_t cmd_ring[QUEUE_CMD_CAP];
static uint8_t cmd_head, cmd_tail, cmd_depth;
static queue_reject_reason_t reject_log[QUEUE_REJECT_LOG_CAP];
static uint8_t reject_head, reject_depth;
static uint32_t reject_total, queue_full_rejects;
static bool save_pending;
static uint16_t debounce_left;
static bool first_frame;

void diag_push(diag_key_t key, uint32_t value);
uint32_t diag_read(diag_key_t key);

static void push_reject(queue_reject_reason_t reason) {
	if (reason == QUEUE_REJECT_NONE) return;
	reject_log[reject_head] = reason;
	reject_head = (uint8_t)((reject_head + 1u) % QUEUE_REJECT_LOG_CAP);
	if (reject_depth < QUEUE_REJECT_LOG_CAP) reject_depth++;
	reject_total++;
	diag.rejects_total = reject_total;
	diag.last_reject = reason;
	if (reason == QUEUE_REJECT_QUEUE_FULL) {
		queue_full_rejects++;
		diag.queue_full_rejects = queue_full_rejects;
	}
}

void queue_init(void) {
	diag.input_high_water = 0u;
	diag.cmd_high_water = 0u;
	diag.cmd_depth = 0u;
	diag.save_depth = 0u;
	diag.rejects_total = 0u;
	diag.queue_full_rejects = 0u;
	diag.last_reject = QUEUE_REJECT_NONE;
	diag.debounce_msleft = 0u;
	cmd_head = cmd_tail = cmd_depth = 0u;
	reject_head = reject_depth = 0u;
	reject_total = queue_full_rejects;
	save_pending = false;
	debounce_left = 0u;
	first_frame = true;
}

static void drain_inputs(uint32_t frame) {
	input_event_t events[QUEUE_INPUT_CAP];
	const uint8_t got = input_frame(events);
	if (got > diag.input_high_water) {
		diag.input_high_water = got;
	}
	for (uint8_t i = 0; i < got; ++i) {
		const input_event_t *ev = &events[i];
		if (ev->kind == INPUT_TOUCH_TAP) {
			if (debounce_left > 0u) {
				debounce_left--;
				continue;
			}
			debounce_left = (uint16_t)DEBOUNCE_FRAMES;
			ui_input((uint8_t)UI_EVT_TOUCH, ev->value);
			continue;
		}
		(void)frame;
		/* The conduit routes every key edge to the active screen
		 * (spec-01 T009); command emission ships with B08/B12.       */
		ui_input((uint8_t)UI_EVT_KEY, ev->value);
	}
}

/* Accept one command; the monotonic per-boot seq is stamped by loop.    */
bool queue_enqueue_cmd(const sim_cmd_t *cmd) {
	if (cmd == NULL) {
		push_reject(QUEUE_REJECT_BAD_ARG);
		return false;
	}
	if (cmd_depth == QUEUE_CMD_CAP) {
		/* spec-02 R7/T010: queue full -> drop the NEWEST, keep the 32,
		 * reject log +1, 0 silent drops.                              */
		push_reject(QUEUE_REJECT_QUEUE_FULL);
		return false;
	}
	cmd_ring[cmd_head] = *cmd;
	cmd_head = (uint8_t)((cmd_head + 1u) % QUEUE_CMD_CAP);
	cmd_depth++;
	diag.cmd_depth = cmd_depth;
	if (cmd_depth > diag.cmd_high_water) {
		diag.cmd_high_water = cmd_depth;
	}
	return true;
}

static void dispatch_commands(uint32_t frame) {
	(void)frame;
	while (cmd_depth > 0u) {
		const sim_cmd_t cmd = cmd_ring[cmd_tail];
		cmd_tail = (uint8_t)((cmd_tail + 1u) % QUEUE_CMD_CAP);
		cmd_depth--;
		diag.cmd_depth = cmd_depth;
		/* sim owns the application; loop only hands the envelope over. */
		/* B03 keeps a single-writer: sim_step consumes the ring next.  */
		(void)cmd;
	}
}

static void coalesce_save(void) {
	/* Spec-02 T012: the save boundary is Base exit or the 5-min tick,
	 * never an action or a tick itself. B03 keeps the depth at 1.       */
	diag.save_depth = save_pending ? 1u : 0u;
}

void queue_frame(uint32_t frame) {
	drain_inputs(frame);
	dispatch_commands(frame);
	coalesce_save();
	if ((frame % TICK_EVERY_FRAMES) == 0u && !first_frame) {
		/* 10 Hz tick boundary; sim_step is called by loop once.        */
	}
	if (debounce_left > 0u) debounce_left--;
	diag.debounce_msleft = (uint16_t)(debounce_left * 16u);
	first_frame = false;
}

uint8_t queue_frame_rejects(void) {
	return reject_depth;
}

const queue_diag_t *queue_diag(void) {
	return &diag;
}

/* result.c - the payoff ledger (B10, spec-05 R1-R9).
 * One line per changed value with a signed delta and a destination hint;
 * Continue applies the sealed outcome exactly once and returns to
 * Campaign with the changed row focused; Retry restores the pre-battle
 * state and the same seed (spec-05 R6). Tick frozen here (loop run-mode
 * gate, spec-05 R1). The pixel stamp and BG budget land with B27/B31.
 */
#include <nds.h>
#include <stdio.h>
#include <string.h>

#include "contracts/sim_api.h"
#include "contracts/save.h"
#include "contracts/ui.h"

#include "ui_draw.h"
#include "screens.h"

static int focus_index;   /* 0 = Continue, 1 = Retry (defeat only)      */
static uint32_t cmd_seq;  /* monotonic per-boot idempotency key         */

static bool enqueue_cmd(uint8_t cmd_id, uint8_t arg) {
	sim_cmd_t cmd;
	memset(&cmd, 0, sizeof cmd);
	cmd.seq = ++cmd_seq;
	cmd.cmd = cmd_id;
	cmd.arg = arg;
	return sim_enqueue(&cmd);
}

void result_input(uint8_t kind, uint32_t value) {
	if (kind != (uint8_t)UI_EVT_KEY) {
		return;
	}
	const battle_outcome_t *o = sim_seal_outcome();
	if (o == NULL) {
		return;
	}
	const bool defeat = (o->result == 0u);
	const int focused = defeat ? focus_index : 0;

	if (value & (KEY_UP | KEY_DOWN)) {
		if (defeat) {
			focus_index ^= 1; /* Retry reachable in 1 tap (R5)      */
		}
		return;
	}
	if (value & KEY_A) {
		if (focused == 1) {
			/* Retry: same seed, pre-battle state, <= 3 frames (R6) */
			enqueue_cmd(6u, 0u);
		} else {
			/* Continue: idempotent apply at dispatch (R4/R8): a
			 * double press draws one delta, queue returns 0.       */
			enqueue_cmd(5u, 0u);
		}
	}
	/* B stays inert: no cancel exists (spec-05 6.4).                     */
}

void result_frame(void) {
	char line[80];
	const battle_outcome_t *o = sim_seal_outcome();
	if (o == NULL) {
		ui_draw_text(0, 0, "RESULT: awaiting the sealed outcome");
		return;
	}
	const sim_snapshot_t *snap = sim_snapshot();
	const sim_province_record_t *tab = sim_province_table(NULL);
	const bool defeat = (o->result == 0u);
	const bool applied = sim_outcome_applied();
	const int focused = defeat ? focus_index : 0;

	snprintf(line, sizeof line, "%s   %s     [%s]",
	         tab[o->province_id].name, defeat ? "DEFEAT" : "VICTORY",
	         defeat ? "REPELLED" : "CONFIRMED");
	ui_draw_text(0, 0, line);
	ui_draw_text(0, 1, "---------------------------------------");

	/* ledger: value / where / signed delta > resulting value (R2)        */
	const long projected_treasury = applied
	    ? (long)snap->treasury
	    : (long)snap->treasury + (long)o->treasury_delta;
	snprintf(line, sizeof line, "TREASURY  (Campaign)  %+ld > %lu",
	         (long)o->treasury_delta, (unsigned long)projected_treasury);
	ui_draw_text(0, 2, line);
	snprintf(line, sizeof line, "DAY       (Campaign)  +%u > %u",
	         (unsigned)o->day_delta, (unsigned)snap->day);
	ui_draw_text(0, 3, line);
	snprintf(line, sizeof line, "DIVISIONS (Battle)    %u lost of %u",
	         (unsigned)(o->units_before - o->units_after),
	         (unsigned)o->units_before);
	ui_draw_text(0, 4, line);
	snprintf(line, sizeof line, "FRONT %-11s (Campaign)  %s",
	         tab[o->province_id].name,
	         sim_front_taken(o->province_id) ? "TAKEN" : "HELD");
	ui_draw_text(0, 5, line);
	snprintf(line, sizeof line, "TIER      (Base)      %u > %u",
	         (unsigned)o->tier_before, (unsigned)o->tier_after);
	ui_draw_text(0, 6, line);

	if (o->treasury_delta == 0 && o->day_delta == 0 &&
	    o->units_before == o->units_after) {
		/* R3: Empty ledger carries the next step, not a zero list  */
		ui_draw_text(0, 8, "Nothing changed. Recruit and go again.");
	} else {
		ui_draw_text(0, 8, "See it on Campaign: front row focus.");
	}

	/* action sheet; defeat keeps Retry beside Continue (R5)              */
	snprintf(line, sizeof line, "[ CONTINUE apply ]%s",
	         (focused == 0) ? "<" : "");
	ui_draw_text(0, 12, line);
	ui_draw_text(0, 13, save_status_text(save_status()));
	ui_draw_text(0, 14, "TICK PAUSED");
	if (defeat) {
		snprintf(line, sizeof line, "[ RETRY same seed ]%s",
		         (focused == 1) ? "<" : "");
		ui_draw_text(0, 15, line);
	}
}

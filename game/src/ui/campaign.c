/* campaign.c - the war-table index (B08, spec-02 R1-R7).
 * 8 province rows x 5 fields x 1 action; affordability is recomputed from
 * the live treasury every frame (R3); a confirm enqueues exactly one
 * recruit command and opens the Base Recruit tab with the province preset
 * (R4); an unaffordable or taken row enqueues nothing (R5). The pixel
 * 4-row paging layout lands with B27 (walking-skeleton text fidelity).
 */
#include <nds.h>
#include <stdio.h>
#include <string.h>

#include "contracts/sim_api.h"
#include "contracts/save.h"
#include "contracts/ui.h"

#include "ui_draw.h"
#include "screens.h"

/* ---- state -------------------------------------------------------------- */
/* focus 0..7 across the 8-row list; L/R still pages by 4 (R1: 8 rows
 * reachable in 2 pages).                                                 */
static int focus;
static ui_screen_t request = UI_SCREEN_NONE;
static char status_line[88];
static int status_frames;

/* T030: the success footer holds 160 ms (10 frames @ 60 Hz) before the
 * Base handoff lands; A is locked out over the hold so a mashed press
 * can never enqueue a second recruit (B12 T032, 20 presses = 1 command). */
static int handoff_hold;
static bool confirm_locked;

static uint32_t cmd_seq;   /* monotonic per boot; idempotency key (B12) */

static const char *threat_label(uint8_t t) {
	switch (t) {
	case SIM_THREAT_LOW: return "LOW";
	case SIM_THREAT_MEDIUM: return "MED";
	case SIM_THREAT_HIGH: return "HIGH";
	default: return "CRIT";
	}
}

void campaign_focus_province(uint8_t province) {
	focus = (province < SIM_FRONT_COUNT) ? (int)province : 0;
	/* every entry into Campaign clears a stale pending handoff        */
	request = UI_SCREEN_NONE;
	handoff_hold = 0;
	confirm_locked = false;
}

void campaign_input(uint8_t kind, uint32_t value) {
	if (kind == (uint8_t)UI_EVT_KEY) {
		const uint32_t keys = value;
		if (keys & KEY_UP) {
			focus = (focus + (int)SIM_FRONT_COUNT - 1) %
			        (int)SIM_FRONT_COUNT;
		} else if (keys & KEY_DOWN) {
			focus = (focus + 1) % (int)SIM_FRONT_COUNT;
		} else if (keys & KEY_LEFT) {
			/* page jump by 4 rows (8 rows, 2 pages, R1)           */
			focus = (focus % 4) + 4;
			focus %= (int)SIM_FRONT_COUNT;
		} else if (keys & KEY_RIGHT) {
			focus = (focus % 4) + 4;
			focus %= (int)SIM_FRONT_COUNT;
		} else if (keys & KEY_B) {
			return; /* B Back is unrouted pre-B12 (resolution on save) */
		} else if (keys & KEY_A) {
			if (confirm_locked) {
				return; /* T032: over the 160 ms hold A is mute   */
			}
			const uint32_t treasury =
			    (uint32_t)((const sim_snapshot_t *)sim_snapshot())->treasury;
			const sim_province_record_t *tab = sim_province_table(NULL);
			const sim_province_record_t *r = &tab[focus];
			if (sim_front_taken((uint8_t)focus)) {
				snprintf(status_line, sizeof status_line,
				         "Front taken. Pick an open row.");
				status_frames = 120;
				return; /* R5: nothing enqueued                  */
			}
			if (treasury < r->cost) {
				/* R5: deficit inline, input preserved         */
				snprintf(status_line, sizeof status_line,
				         "%s needs %u more. Treasury %lu.",
				         r->name,
				         (unsigned)(r->cost - treasury),
				         (unsigned long)treasury);
				status_frames = 120;
				return;
			}
			sim_cmd_t cmd;
			memset(&cmd, 0, sizeof cmd);
			cmd.seq = ++cmd_seq;
			cmd.cmd = 0u; /* recruit */
			cmd.arg = (uint8_t)focus;
			if (sim_enqueue(&cmd)) {
				/* T028 success line, byte-exact (spec-02 6.x);
				 * treasury shown is post-debit.               */
				snprintf(status_line, sizeof status_line,
				         "%s: recruit queued, %u debited. "
				         "Treasury %lu. Opening Recruit tab.",
				         r->name, (unsigned)r->cost,
				         (unsigned long)(treasury - r->cost));
				status_frames = 10; /* 160 ms (spec-02 T028) */
				handoff_hold = 10;
				confirm_locked = true;
			} else {
				/* R7: 32/32 queued, inline, input preserved   */
				snprintf(status_line, sizeof status_line,
				         "Not accepted: 32/32 queued.");
				status_frames = 120;
			}
		}
	}
}

ui_screen_t campaign_request(void) {
	const ui_screen_t r = request;
	request = UI_SCREEN_NONE;
	return r;
}

/* B13 (spec-02 T033): the focused row, read only at the commit
 * boundary by ui -> queue; an out-of-time screen reports 0xFF.           */
uint8_t campaign_focus_row(void) {
	return (focus >= 0 && focus < (int)SIM_FRONT_COUNT)
	           ? (uint8_t)focus
	           : 0xFFu;
}

void campaign_frame(void) {
	char line[80];
	char action[16];
	const sim_snapshot_t *snap = sim_snapshot();
	const sim_province_record_t *tab = sim_province_table(NULL);

	/* T030: the Base handoff lands after the 160 ms success hold.       */
	if (handoff_hold > 0 && request == UI_SCREEN_NONE) {
		if (--handoff_hold == 0) {
			request = UI_SCREEN_BASE; /* R4 handoff: the preset   */
			confirm_locked = false;
		}
	}

	/* header: live treasury ticker + day (R2: <= 1 tick lag)             */
	snprintf(line, sizeof line, "TREASURY %-9lu +2/DAY   DAY %u",
	         (unsigned long)snap->treasury, (unsigned)snap->day);
	ui_draw_text(0, 0, line);
	ui_draw_text(0, 1, "NAME         COST THREAT   REWARD  ACTION");

	/* 8 rows, 5 rendered fields + 1 action each (R1 census)              */
	for (int i = 0; i < (int)SIM_FRONT_COUNT; ++i) {
		const sim_province_record_t *r = &tab[i];
		if (sim_front_taken((uint8_t)i)) {
			snprintf(action, sizeof action, "%-9s", "TAKEN");
		} else if (snap->treasury >= r->cost) {
			snprintf(action, sizeof action, "%s",
			         (focus == i) ? "[*]" : "[RECRUIT]");
		} else {
			/* R3: the exact deficit inline, disabled state       */
			snprintf(action, sizeof action, "NEED %-4u",
			         (unsigned)(r->cost - snap->treasury));
		}
		snprintf(line, sizeof line, "%s%-11s %3u %-4s %5u %s",
		         (focus == i) ? ">" : " ", r->name,
			        (unsigned)r->cost, threat_label(r->threat),
			        (unsigned)r->reward, action);
		ui_draw_text(0, 2 + i, line);
	}

	/* inline status / footer                                             */
	if (status_frames > 0) {
		status_frames--;
		ui_draw_text(0, 12, status_line);
	} else {
		ui_draw_text(0, 12, "A Recruit . L/R page . START pause B12");
	}
}

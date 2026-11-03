/* base.c - the Base hub: Recruit stepper + Formation (B09, spec-03 R1-R8).
 * The economy tick pauses here via the run mode (loop.c gate), the muster
 * keeps rolling (R4 pending strip). No modals: every block shows its
 * reason inline with focus and input preserved (R3). Tier band pixel
 * geometry and the N/10 hash land with B16/B17/B27.
 */
#include <nds.h>
#include <stdio.h>
#include <string.h>

#include "contracts/sim_api.h"
#include "contracts/save.h"
#include "contracts/audio.h"
#include "contracts/ui.h"

#include "ui_draw.h"
#include "base_tier.h"
#include "screens.h"

/* ---- state -------------------------------------------------------------- */
static int tab;                     /* 0 = Recruit, 1 = Formation        */
static int focus;                   /* per-tab target index              */
static ui_screen_t request;
static char status_line[64];
static int status_frames;

static int first_selected;          /* pending formation swap (slot 0..9) */
static bool swap_pending;
static int last_touch = -1;         /* B16 touch parity tap state         */

static uint32_t cmd_seq;            /* monotonic per-boot idempotency key */

/* ---- helpers ------------------------------------------------------------- */

static bool enqueue_cmd(uint8_t cmd_id, uint8_t arg);

static const char *tier_word(unsigned tier) {
	if (tier == 0u) return "OUTPOST";
	if (tier <= 2u) return "WARD";
	if (tier <= 5u) return "GARRISON";
	if (tier <= 8u) return "STRONGHOLD";
	return "FORTRESS (max)";
}

static uint8_t tier(void) {
	/* Tier source = open-question-3 default: campaign victories.        */
	uint8_t t = 0u;
	for (uint8_t i = 0; i < SIM_FRONT_COUNT; ++i) {
		if (sim_front_taken(i)) {
			t++;
		}
	}
	return t;
}

/* Blocked reasons inline (spec-03 R3: "0 modals in 20 attempts").        */
static bool confirm_blocked(char *reason, unsigned reason_sz) {	const sim_snapshot_t *snap = sim_snapshot();
	const uint32_t cost = (uint32_t)SIM_COMPANY_COST * SIM_PULSE_QTY;
	if (sim_chest() + SIM_PULSE_QTY > SIM_CHEST_CAP) {
		snprintf(reason, reason_sz, "! Chest %u/%u: +5 needs %u.",
		         (unsigned)sim_chest(), SIM_CHEST_CAP,
		         SIM_CHEST_CAP);
		return true;
	}
	if (snap->treasury < cost) {
		snprintf(reason, reason_sz, "! Treasury %lu g: +5 needs %u g.",
		         (unsigned long)snap->treasury, (unsigned)cost);
		return true;
	}
	if (sim_muster_total() + SIM_PULSE_QTY > SIM_PENDING_CAP) {
		snprintf(reason, reason_sz, "! Queue full %u/%u.",
		         (unsigned)sim_muster_total(), SIM_PENDING_CAP);
		return true;
	}
	return false;
}

static void say(const char *text) {
	snprintf(status_line, sizeof status_line, "%s", text);
	status_frames = 150;
}

/* One command through the conduit; one debit at dispatch (B12 owns the
 * duplicate guard and the dispatch re-check copy).                       */
static bool enqueue_cmd(uint8_t cmd_id, uint8_t arg) {
	sim_cmd_t cmd;
	memset(&cmd, 0, sizeof cmd);
	cmd.seq = ++cmd_seq;
	cmd.cmd = cmd_id;
	cmd.arg = arg;
	return sim_enqueue(&cmd);
}

/* Deploy door (spec-03 R1/§6: one forward door only, from Base).         */
static void deploy_or_block(void) {
	if (sim_roster() == 0u) {
		say("Deploy blocked: 0 companies in formation. Recruit first.");
	} else if (sim_deploy_province() < 0) {
		say("No front preset. Recruit one on Campaign.");
	} else if (enqueue_cmd(4u, 0xFFu)) {
		request = UI_SCREEN_BATTLE;
	}
}

static bool deploy_focus_now(void) {
	const int deploy_focus = (tab == 0) ? 2 : SIM_FORMATION_SLOTS;
	return (focus == deploy_focus);
}


/* Per-tab focus targets: Recruit offers the stepper, Confirm and Deploy;
 * Formation offers the 10 slot cards and Deploy.                        */
static int target_count(void) {
	return (tab == 0) ? 3 : SIM_FORMATION_SLOTS + 1;
}

/* The A action on the current focus, shared by the key and the touch
 * path (B16 touch parity: two taps = ring then confirm, like the title).*/
static void do_action(void) {
	char reason[64];
	if (tab == 0) {
		switch (focus) {
		case 0: /* the +5 stepper: one command per press        */
			if (confirm_blocked(reason, sizeof reason)) {
				say(reason); /* R3: nothing enqueued   */
				audio_cue(AUDIO_CUE_FAILURE); /* T013 */
			} else {
				enqueue_cmd(3u, SIM_PULSE_QTY);
				say("+5 queued (1 per 2 s).");
				audio_cue(AUDIO_CUE_CONFIRM);
			}
			break;
		case 1: /* Confirm: same +5 command                     */
			if (confirm_blocked(reason, sizeof reason)) {
				say(reason);
				audio_cue(AUDIO_CUE_FAILURE);
			} else {
				enqueue_cmd(3u, SIM_PULSE_QTY);
				say("5 queued: 60 g. Muster 1 per 2 s.");
				audio_cue(AUDIO_CUE_CONFIRM);
			}
			break;
		case 2: /* Deploy to the preset front                   */
			deploy_or_block();
			break;
		default:
			break;
		}
	} else {
		if (focus == SIM_FORMATION_SLOTS) {
			deploy_or_block();
		} else if (!swap_pending) {
			first_selected = focus; /* slot select (R7)    */
			swap_pending = true;
			snprintf(reason, sizeof reason,
			         "Slot %d selected - pick target slot A.",
			         focus + 1);
			say(reason);
		} else if (swap_pending && (focus == first_selected)) {
			say("Pick a different slot.");
		} else {
			/* the swap enqueues through the conduit       */
			const uint8_t pair = (uint8_t)((first_selected << 4u) | focus);
			if (enqueue_cmd(1u, pair)) {
				swap_pending = false;
				say("Swapped pairs in line.");
			}
		}
	}
}

/* B16 touch parity (spec-03 T019): tap 1 moves the ring, tap 2 on the
 * same target commits. The hit map is the spec-03 32-px geometry:
 * Recruit rows stepper / Confirm / Deploy; Formation = 2 rows x 5
 * slot cards of 32 x 51 px, then the Deploy door. -1 = outside.         */
static int touch_target(uint32_t value) {
	const uint32_t px = (value >> 8) & 0xFFu;
	const uint32_t py = value & 0xFFu;
	if (py < 24u || py >= 128u) {
		return -1;
	}
	if (tab == 0) {
		if (py < 64u) return 0;
		if (py < 96u) return 1;
		return 2;
	}
	const int row = (py < 64u) ? 0 : 1;
	const int col = (px < 51u) ? 0
	    : (px < 102u) ? 1
	    : (px < 153u) ? 2
	    : (px < 204u) ? 3 : 4;
	return row * 5 + col;
}

void base_input(uint8_t kind, uint32_t value) {
	if (kind == (uint8_t)UI_EVT_TOUCH) {
		const int t = touch_target(value);
		if (t >= 0) {
			if (t == last_touch) {
				/* tap 2 on the same target: confirm (spec-01
				 * 6.2 pattern: ring first, then act)         */
				last_touch = -1;
				do_action();
			} else {
				/* tap 1: the ring moves, nothing enqueued    */
				focus = t;
				last_touch = t;
			}
		}
		return;
	}
	if (kind != (uint8_t)UI_EVT_KEY) {
		return;
	}
	/* Any key edge retires the touch pending: the ring commits by key   */
	last_touch = -1;
	const uint32_t keys = value;
	if (keys & (KEY_UP | KEY_DOWN)) {
		focus += (keys & KEY_UP) ? -1 : 1;
		if (focus < 0) {
			focus = target_count() - 1;
		} else if (focus >= target_count()) {
			focus = 0;
		}
	} else if (keys & (KEY_LEFT | KEY_RIGHT)) {
		tab ^= 1;
		focus = 0;
	} else if (keys & KEY_B) {
		request = UI_SCREEN_CAMPAIGN; /* exit save boundary = coalesce */
	} else if (keys & KEY_A) {
		do_action();
	}
}

ui_screen_t base_request(void) {
	const ui_screen_t r = request;
	request = UI_SCREEN_NONE;
	return r;
}

void base_frame(void) {
	char line[80];
	const sim_snapshot_t *snap = sim_snapshot();
	const uint8_t tier_now = tier();

	/* B17: the 24 px band model repaints once per tier change (R9);
	 * the pixel blit lands with the B27/B31 art sweep. The text row
	 * beside it carries the word copy for the emulator console.          */
	tier_band_paint(tier_now);

	/* tier band (24 px pixel geometry lands with B17; text now)          */
	snprintf(line, sizeof line, "TIER %u - %s",
	         (unsigned)tier_now, tier_word(tier_now));
	ui_draw_text(0, 0, line);

	if (tab == 0) {
		/* Recruit tab                                                   */
		const uint32_t cost =
		    (uint32_t)SIM_COMPANY_COST * SIM_PULSE_QTY;
		char reason[64];
		const bool blocked = confirm_blocked(reason, sizeof reason);
		snprintf(line, sizeof line,
		         "CHEST %u/%u   %lu g   next -%lu g",
		         (unsigned)sim_chest(), SIM_CHEST_CAP,
		         (unsigned long)snap->treasury, (unsigned long)cost);
		ui_draw_text(0, 2, line);
		snprintf(line, sizeof line,
		         "[-]  QTY %u = %u companies  [+]%s",
		         SIM_PULSE_QTY, SIM_PULSE_QTY,
			         (focus == 0) ? " <" : "");
		ui_draw_text(0, 4, line);
		snprintf(line, sizeof line, "%sCONFIRM 5 -%lu g",
		         blocked ? "<" : "", (unsigned long)cost);
		ui_draw_text(0, 6, line);
		/* Muster states, byte-exact copy (spec-03:151/154, T012):
		 * Loading `Mustering n of m - 0:xx. n queued.`, Partial
		 * `3 of 5 mustered - 0:04 left. 2 still in queue.`, Success
		 * `5 mustered. Chest x/y. Treasury z g.` The remaining-time
		 * math is the batch's own ticks at 10 Hz: m*20 - elapsed.     */
		const uint32_t done = sim_muster_done();
		const uint32_t total = sim_muster_total();
		if (total > 0u && done < total) {
			const uint32_t prog = SIM_MUSTER_TICKS -
			                      sim_muster_ticks_left();
			const uint32_t remain = total - done; /* incl. current */
			const uint32_t left_ticks =
			    remain * SIM_MUSTER_TICKS - prog;
			const uint32_t left_s = (left_ticks + 9u) / 10u;
			if (done == 0u) {
				/* Loading (spec-03:151): the items stay card
				 * pips - no spinner, one inline line.         */
				snprintf(line, sizeof line,
				         "Mustering 1 of %u - 0:%02u. "
				         "%u queued.",
				         (unsigned)total, (unsigned)left_s,
				         (unsigned)remain);
			} else {
				/* Partial (spec-03:154): byte-exact copy.     */
				snprintf(line, sizeof line,
				         "%u of %u mustered - 0:%02u left. "
				         "%u still in queue.",
				         (unsigned)done, (unsigned)total,
				         (unsigned)left_s, (unsigned)remain);
			}
		} else if (total > 0u) {
			snprintf(line, sizeof line,
			         "%u mustered. Chest %u/%u. Treasury %lu g.",
			         (unsigned)total, (unsigned)sim_chest(),
			         (unsigned)SIM_CHEST_CAP,
			         (unsigned long)snap->treasury);
		} else {
			snprintf(line, sizeof line,
			         "No companies. Chest %u/%u. +5 costs %u g.",
			         (unsigned)sim_chest(), SIM_CHEST_CAP,
			         (unsigned)cost);
		}
		ui_draw_text(0, 8, line);
	} else {
		/* Formation tab (B16, spec-03:152-154 byte-exact copy):
		 * Empty `0/10. No companies to form. Recruit from the
		 * Recruit tab.`; Partial `4 of 10 filled, 6 Empty.
		 * Recruit 6 more, or deploy with 4.`; Full `10 of 10
		 * filled, 0 Empty.`                                       */
		const uint8_t *form = sim_formation();
		uint8_t filled = 0u;
		for (int i = 0; i < (int)SIM_FORMATION_SLOTS; ++i) {
			if (form[i] != 0xFFu) {
				filled++;
			}
		}
		if (filled == 0u) {
			snprintf(line, sizeof line,
			         "%u/10. No companies to form. Recruit from"
			         " the Recruit tab.", (unsigned)filled);
		} else if (filled < SIM_FORMATION_SLOTS) {
			snprintf(line, sizeof line,
			         "%u of 10 filled, %u Empty. Recruit %u more,"
			         " or deploy with %u.",
			         (unsigned)filled, (unsigned)(10u - filled),
			         (unsigned)(SIM_FORMATION_SLOTS - filled),
			         (unsigned)filled);
		} else {
			snprintf(line, sizeof line, "10 of 10 filled, 0 Empty.");
		}
		ui_draw_text(0, 2, line);
		for (int row = 0; row < 2; ++row) {
			line[0] = '\0';
			for (int col = 0; col < 5; ++col) {
				const int i = row * 5 + col;
				const int n = snprintf(line + strlen(line),
				                       sizeof line - strlen(line),
				                       "|[%02d]%s%s", i + 1,
				                       (form[i] != 0xFFu) ? "C" : " ",
				                       (focus == i ? "<" : " "));
				(void)n;
			}
			ui_draw_text(0, 4 + row, line);
		}
		if (swap_pending) {
			ui_draw_text(0, 7, "Slot picked - A on a second slot");
		}
	}

	/* deploy door + SaveStatus inline footer                             */
	const char *front = "-";
	if (sim_deploy_province() >= 0) {
		front = sim_province_table(NULL)[sim_deploy_province()].name;
	}
	snprintf(line, sizeof line, "Deploy to %s%s",
	         front, (deploy_focus_now()) ? " <" : "");
	ui_draw_text(0, 12, line);
	ui_draw_text(0, 13, save_status_text(save_status()));
	if (status_frames > 0) {
		status_frames--;
		ui_draw_text(0, 14, status_line);
	} else {
		ui_draw_text(0, 14, "B Campaign . A confirm");
	}
}

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
#include "contracts/render.h"

#include "ui_draw.h"
#include "base_tier.h"
#include "screens.h"

/* ---- state -------------------------------------------------------------- */
static int tab;                     /* 0 = Recruit, 1 = Formation        */
static int focus;                   /* per-tab target index              */
static ui_screen_t request = UI_SCREEN_NONE; /* 0 would be TITLE */
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

/* Formation tab targets: the 10 slot cards, the formation-kind cycler
 * (GAME_DESIGN 7: a general formation, its divisions arranged inside),
 * then the Deploy door.                                                  */
#define KIND_FOCUS ((int)SIM_FORMATION_SLOTS)
#define FORM_DEPLOY_FOCUS ((int)SIM_FORMATION_SLOTS + 1)

static bool deploy_focus_now(void) {
	const int deploy_focus = (tab == 0) ? 2 : FORM_DEPLOY_FOCUS;
	return (focus == deploy_focus);
}

static void cycle_kind(void) {
	char line[64];
	const uint8_t next = (uint8_t)((sim_formation_kind() + 1u) % SIM_FORM_COUNT);
	if (enqueue_cmd(11u, next)) {
		snprintf(line, sizeof line, "%s: %s", sim_formation_name(next),
		         sim_formation_brief(next));
		say(line);
		audio_cue(AUDIO_CUE_CONFIRM);
	}
}


/* Per-tab focus targets: Recruit offers the stepper, Confirm and Deploy;
 * Formation offers the 10 slot cards, the kind cycler and Deploy.       */
static int target_count(void) {
	return (tab == 0) ? 3 : FORM_DEPLOY_FOCUS + 1;
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
		if (focus == FORM_DEPLOY_FOCUS) {
			deploy_or_block();
		} else if (focus == KIND_FOCUS) {
			cycle_kind();
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
	if (py >= 104u) {
		return (px < 124u) ? KIND_FOCUS : FORM_DEPLOY_FOCUS;
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

/* ---- art pass ----------------------------------------------------------- */
static const ui_row_t BASE_ROWS[] = {
	{0, RENDER_BOTTOM, 160, 8, 96, 1, RENDER_LINE},
	{8, RENDER_BOTTOM, 8, 130, 240, 2, RENDER_INK},
	{2, RENDER_BOTTOM, 8, 130, 240, 2, RENDER_INK}, /* Formation N/10 */
	{7, RENDER_BOTTOM, 8, 150, 240, 1, RENDER_ACCENT},
	{14, RENDER_BOTTOM, 8, 166, 240, 3, RENDER_INK},
};
/* the Recruit tab's chest row stays semantic only (painted below)     */
static const ui_row_t BASE_ROWS_RECRUIT[] = {
	{0, RENDER_BOTTOM, 160, 8, 96, 1, RENDER_LINE},
	{8, RENDER_BOTTOM, 8, 130, 240, 2, RENDER_INK},
	{14, RENDER_BOTTOM, 8, 166, 240, 3, RENDER_INK},
};

/* company id -> the arm it marches as (4 arms, deterministic)         */
static uint8_t arm_of(uint8_t id) {
	static const uint8_t ARM[4] = {RENDER_CLS_SPEAR, RENDER_CLS_BLADE,
	                               RENDER_CLS_ARCHER, RENDER_CLS_CAVALRY};
	return ARM[id & 3u];
}

static void paint_top(const sim_snapshot_t *snap, uint8_t tier_now) {
	char buf[48];
	const uint32_t step = ui_anim_step();
	render_backdrop(RENDER_TOP, RENDER_BD_MAP_PLAINS, RENDER_GOD_NONE);
	render_camera(40, 120);
	/* every mustered company stands on the parade ground (10 shown);
	 * a formed slot marches as its own arm                            */
	const uint8_t *form = sim_formation();
	const uint16_t roster = sim_roster();
	for (int i = 0; i < (int)SIM_FORMATION_SLOTS && i < (int)roster; ++i) {
		const uint8_t id = (form[i] != 0xFFu) ? form[i] : (uint8_t)i;
		const int x = 24 + (i % 5) * 40 + (i / 5) * 12;
		const int y = 20 + (i / 5) * 24;
		render_squad(RENDER_CIV_HELLENES, arm_of(id), 0u,
		             RENDER_ANIM_IDLE, step + (uint32_t)i, x, y, 0u);
	}
	if (sim_roster() > 0u) {
		render_squad(RENDER_CIV_HELLENES, RENDER_CLS_GENERAL, 0u, 2u, step,
		             4, 28, 0u);
	}
	/* the muster: companies still in the queue wait at the camp edge   */
	const uint32_t queued = sim_muster_total() - sim_muster_done();
	for (uint32_t k = 0; k < queued && k < 5u; ++k) {
		render_squad(RENDER_CIV_HELLENES, RENDER_CLS_SPEAR, 3u,
		             RENDER_ANIM_WALK, step + k, 200 + (int)k * 6,
		             84 + (int)k * 4, 0u);
	}
	/* the relic shelf (GAME_DESIGN 8/15): every god beaten stays on
	 * show at the camp; an empty niche says what is still out there   */
	{
		const uint8_t relics = sim_relic_mask();
		render_rect(RENDER_TOP, 150, 2, 104, 22, RENDER_INK);
		render_text(RENDER_TOP, 154, 4, RENDER_LINE, "Relics");
		for (uint8_t g = 0; g < SIM_BOSSES; ++g) {
			const int x = 154 + (int)g * 20;
			if (relics & (1u << g)) {
				render_relic(RENDER_TOP, g, x - 2, 8, RENDER_SPR_FRONT);
			} else {
				render_box(RENDER_TOP, x, 13, 12, 9, RENDER_IRON);
			}
		}
	}
	render_panel(RENDER_TOP, 8, 132, 240, 54, RENDER_PANEL_IRON);
	render_text(RENDER_TOP, 16, 139, RENDER_LINE, "Chest");
	snprintf(buf, sizeof buf, "%u/%u", (unsigned)sim_chest(), SIM_CHEST_CAP);
	render_text_big(RENDER_TOP, 16, 150, RENDER_BRONZE_L, buf);
	render_icon(RENDER_TOP, RENDER_ICON_GOLD, 130, 140, RENDER_SPR_FRONT);
	snprintf(buf, sizeof buf, "%lu g", (unsigned long)snap->treasury);
	render_text(RENDER_TOP, 150, 144, RENDER_SURFACE, buf);
	render_icon(RENDER_TOP, RENDER_ICON_TIER, 130, 160, RENDER_SPR_FRONT);
	snprintf(buf, sizeof buf, "Tier %u %s", (unsigned)tier_now,
	         tier_word(tier_now));
	render_text(RENDER_TOP, 150, 164, RENDER_SURFACE, buf);
}

static void paint_tabs(void) {
	static const char *const TABS[2] = {"Recruit", "Formation"};
	for (int i = 0; i < 2; ++i) {
		const int x = 6 + i * 82;
		if (tab == i) {
			render_rect(RENDER_BOTTOM, x, 4, 78, 18, RENDER_PAPER);
		}
		render_text(RENDER_BOTTOM, x + 39 - render_text_width(TABS[i]) / 2, 8,
		            tab == i ? RENDER_INK : RENDER_LINE, TABS[i]);
	}
}

static void paint_deploy_at(int x, int y, int w, int h, bool focused) {
	char buf[40];
	const int8_t dp = sim_deploy_province();
	const bool ok = dp >= 0 && sim_roster() > 0u;
	snprintf(buf, sizeof buf, "Deploy to %s",
	         dp >= 0 ? sim_province_table(NULL)[dp].name : "-");
	render_button(RENDER_BOTTOM, x, y, w, h, buf,
	              !ok ? RENDER_BTN_DISABLED
	                  : focused ? RENDER_BTN_FOCUS : RENDER_BTN_NORMAL);
	render_icon(RENDER_BOTTOM, RENDER_ICON_BATTLE, x + w - 18, y + h / 2 - 8,
	            RENDER_SPR_FRONT);
	if (focused) {
		render_ring(RENDER_BOTTOM, x, y, w, h);
	}
}

static void paint_deploy(int y, int h, bool focused) {
	paint_deploy_at(6, y, 244, h, focused);
}

static void paint(void) {
	char buf[48];
	const uint32_t step = ui_anim_step();
	render_backdrop(RENDER_BOTTOM, RENDER_BD_FRAME_SHEET, RENDER_GOD_NONE);
	paint_tabs();
	if (tab == 0) {
		const uint32_t cost = (uint32_t)SIM_COMPANY_COST * SIM_PULSE_QTY;
		char reason[64];
		const bool blocked = confirm_blocked(reason, sizeof reason);
		render_panel(RENDER_BOTTOM, 6, 26, 244, 36, RENDER_PANEL_SHEET);
		render_portrait(RENDER_BOTTOM, RENDER_CIV_HELLENES, RENDER_CLS_SPEAR,
		                RENDER_ANIM_IDLE, step, 14, 36, RENDER_SPR_FRONT);
		render_portrait(RENDER_BOTTOM, RENDER_CIV_HELLENES, RENDER_CLS_ARCHER,
		                RENDER_ANIM_IDLE, step + 1u, 28, 36, RENDER_SPR_FRONT);
		render_text(RENDER_BOTTOM, 50, 33, RENDER_INK, "Companies");
		snprintf(buf, sizeof buf, "+%u = %lu g. Chest %u/%u.",
		         SIM_PULSE_QTY, (unsigned long)cost, (unsigned)sim_chest(),
		         SIM_CHEST_CAP);
		render_text(RENDER_BOTTOM, 50, 45, RENDER_INK2, buf);
		render_button(RENDER_BOTTOM, 204, 32, 40, 16, "+5",
		              blocked ? RENDER_BTN_DISABLED
		              : focus == 0 ? RENDER_BTN_FOCUS : RENDER_BTN_NORMAL);
		if (focus == 0) {
			render_ring(RENDER_BOTTOM, 6, 26, 244, 36);
		}
		snprintf(buf, sizeof buf, "Confirm %u  -%lu g", SIM_PULSE_QTY,
		         (unsigned long)cost);
		render_button(RENDER_BOTTOM, 6, 68, 244, 24, buf,
		              blocked ? RENDER_BTN_DISABLED
		              : focus == 1 ? RENDER_BTN_FOCUS : RENDER_BTN_NORMAL);
		if (focus == 1) {
			render_ring(RENDER_BOTTOM, 6, 68, 244, 24);
		}
		if (blocked) {
			render_text(RENDER_BOTTOM, 10, 150, RENDER_CRIT, reason);
		}
		paint_deploy(100, 24, focus == 2);
	} else {
		const uint8_t *form = sim_formation();
		for (int i = 0; i < (int)SIM_FORMATION_SLOTS; ++i) {
			const int x = 2 + (i % 5) * 51;
			const int y = 26 + (i / 5) * 40;
			render_panel(RENDER_BOTTOM, x, y, 48, 36, RENDER_PANEL_SHEET);
			snprintf(buf, sizeof buf, "%02d", i + 1);
			render_text(RENDER_BOTTOM, x + 6, y + 6, RENDER_INK2, buf);
			if (form[i] != 0xFFu) {
				render_portrait(RENDER_BOTTOM, RENDER_CIV_HELLENES,
				                arm_of(form[i]), RENDER_ANIM_IDLE,
				                step + (uint32_t)i, x + 24, y + 14,
				                RENDER_SPR_FRONT);
			} else {
				render_text(RENDER_BOTTOM, x + 6, y + 22, RENDER_LINE,
				            "Empty");
			}
			if (swap_pending && first_selected == i) {
				render_box(RENDER_BOTTOM, x + 2, y + 2, 44, 32,
				           RENDER_ACCENT);
			}
			if (focus == i) {
				render_ring(RENDER_BOTTOM, x, y, 48, 36);
			}
		}
		{
			const uint8_t k = sim_formation_kind();
			snprintf(buf, sizeof buf, "< %s >", sim_formation_name(k));
			render_button(RENDER_BOTTOM, 6, 108, 116, 18, buf,
			              focus == KIND_FOCUS ? RENDER_BTN_FOCUS
			                                  : RENDER_BTN_NORMAL);
			render_icon(RENDER_BOTTOM, k == SIM_FORM_CIRCLE || k == SIM_FORM_PHALANX
			                               ? RENDER_ICON_DEFENSE : RENDER_ICON_ATTACK,
			            8, 109, RENDER_SPR_FRONT);
			if (focus == KIND_FOCUS) {
				render_ring(RENDER_BOTTOM, 6, 108, 116, 18);
			}
		}
		paint_deploy_at(126, 108, 124, 18, focus == FORM_DEPLOY_FOCUS);
	}
}

void base_frame(void) {
	char line[80];
	const sim_snapshot_t *snap = sim_snapshot();
	const uint8_t tier_now = tier();

	/* B17: the 24 px band model repaints once per tier change (R9);
	 * the pixel blit lands with the B27/B31 art sweep. The text row
	 * beside it carries the word copy for the emulator console.          */
	tier_band_paint(tier_now);
	if (tab == 0) {
		ui_draw_layout(BASE_ROWS_RECRUIT, (uint8_t)(sizeof BASE_ROWS_RECRUIT /
		                                            sizeof BASE_ROWS_RECRUIT[0]));
	} else {
		ui_draw_layout(BASE_ROWS, (uint8_t)(sizeof BASE_ROWS / sizeof BASE_ROWS[0]));
	}
	paint_top(snap, tier_now);
	paint();

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
		} else if (status_frames == 0) {
			/* the kind's trade-off, and the ground it will meet        */
			const int8_t dp = sim_deploy_province();
			snprintf(line, sizeof line, "%s: %s%s%s",
			         sim_formation_name(sim_formation_kind()),
			         sim_formation_brief(sim_formation_kind()),
			         dp >= 0 ? " " : "",
			         dp >= 0 ? sim_terrain_brief(sim_front_terrain((uint8_t)dp)) : "");
			ui_draw_text(0, 7, line);
		}
		snprintf(line, sizeof line, "FORMATION %s%s",
		         sim_formation_name(sim_formation_kind()),
		         focus == KIND_FOCUS ? " <" : "");
		ui_draw_text(0, 10, line);
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

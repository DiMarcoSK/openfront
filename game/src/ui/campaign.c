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
#include "contracts/render.h"

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
	if (kind == (uint8_t)UI_EVT_TOUCH) {
		/* art pass: 8 rows of 17 px from y 26; tap 1 rings the row,
		 * tap 2 on the same row is the A press (spec-01 6.5 pattern). */
		const int py = (int)(value & 0xFFu);
		if (py < 26 || py >= 26 + 17 * (int)SIM_FRONT_COUNT) {
			return;
		}
		const int row = (py - 26) / 17;
		if (row != focus) {
			focus = row;
			return;
		}
		kind = (uint8_t)UI_EVT_KEY;
		value = KEY_A;
	}
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

/* ---- art pass ----------------------------------------------------------- */
#define ROW_Y0 26
#define ROW_H 17

static const ui_row_t CAMPAIGN_ROWS[] = {
	{0, RENDER_BOTTOM, 8, 8, 244, 1, RENDER_BRONZE_L},
	{12, RENDER_BOTTOM, 8, 166, 244, 3, RENDER_INK},
};

static const char *const THREAT_WORD[4] = {"Low", "Medium", "High", "Critical"};
static const char *const CIV_WORD[RENDER_CIVS] = {
	"Hellenes", "Rome", "Carthage", "Assyria",
};

/* the focused front on the top screen: its field, its enemy line (or
 * its god) and a plate with the five fields of the row (spec-02 R1)    */
static int pan;
static void paint_top(const sim_province_record_t *r, uint8_t i) {
	char buf[48];
	const front_art_t *a = front_art(i);
	const uint32_t step = ui_anim_step();
	render_backdrop(RENDER_TOP, a->map, a->god);
	pan = (pan + 1) % 1024;
	const int cx = 96 + (pan < 512 ? pan : 1024 - pan) / 4;
	render_camera(cx, 150);
	/* the holder of the front, framed above the plate (screen coords) */
	const bool taken = sim_front_taken(i);
	const bool divine = a->god != RENDER_GOD_NONE && !taken;
	if (divine) {
		render_god(a->god, RENDER_GOD_IDLE, step, 64, 2, RENDER_SPR_FLIP);
	}
	for (int k = 0; k < (divine ? 4 : 8); ++k) {
		const int x = (divine ? 150 : 60) + (k % 4) * 34 - (k / 4) * 10;
		const int y = (divine ? 104 : 70) + (k / 4) * 20;
		render_squad(taken ? RENDER_CIV_HELLENES : a->enemy,
		             (k % 4 == 3) ? RENDER_CLS_CAVALRY : RENDER_CLS_SPEAR,
		             0u, RENDER_ANIM_IDLE, step + (uint32_t)k, x, y,
		             taken ? 0u : RENDER_SPR_FLIP);
	}
	if (!divine) {
		render_squad(taken ? RENDER_CIV_HELLENES : a->enemy,
		             RENDER_CLS_GENERAL, 0u, 2u, step, 210, 48,
		             taken ? 0u : RENDER_SPR_FLIP);
	}
	render_panel(RENDER_TOP, 8, 132, 240, 54, RENDER_PANEL_IRON);
	render_pin(RENDER_TOP, r->threat, 16, 140, RENDER_SPR_FRONT);
	render_text_big(RENDER_TOP, 36, 139, RENDER_BRONZE_L, r->name);
	snprintf(buf, sizeof buf, "Threat %s. Cost %u g, reward +%u g.",
	         THREAT_WORD[r->threat & 3u], (unsigned)r->cost,
	         (unsigned)r->reward);
	render_text(RENDER_TOP, 16, 160, RENDER_LINE, buf);
	if (taken) {
		snprintf(buf, sizeof buf, "Taken. Your banner holds it.");
	} else if (a->god != RENDER_GOD_NONE) {
		snprintf(buf, sizeof buf, "%s holds this front.", god_name(a->god));
		render_relic(RENDER_TOP, a->god, 226, 140, RENDER_SPR_FRONT);
	} else {
		snprintf(buf, sizeof buf, "%s holds this front.",
		         CIV_WORD[a->enemy % RENDER_CIVS]);
	}
	render_text(RENDER_TOP, 16, 170, RENDER_SURFACE, buf);
	/* the ground and the stake: what to prepare for (GAME_DESIGN 7-8)  */
	{
		const uint8_t boss = sim_front_boss(i);
		char rb[56];
		if (boss != SIM_BOSS_NONE && !taken) {
			snprintf(rb, sizeof rb, "Relic %s: %s", sim_relic_name(boss),
			         sim_relic_brief(boss));
		} else {
			snprintf(rb, sizeof rb, "%s: %s",
			         sim_terrain_name(sim_front_terrain(i)),
			         sim_terrain_brief(sim_front_terrain(i)));
		}
		render_text(RENDER_TOP, 16, 178, RENDER_LINE, rb);
	}
}

static void paint(const sim_snapshot_t *snap, const sim_province_record_t *tab) {
	char buf[24];
	render_backdrop(RENDER_BOTTOM, RENDER_BD_FRAME_SHEET, RENDER_GOD_NONE);
	paint_top(&tab[focus], (uint8_t)focus);
	for (int i = 0; i < (int)SIM_FRONT_COUNT; ++i) {
		const sim_province_record_t *r = &tab[i];
		const int y = ROW_Y0 + i * ROW_H;
		const bool f = (focus == i);
		render_rect(RENDER_BOTTOM, 4, y, 248, ROW_H - 1,
		            f ? RENDER_SURFACE : RENDER_PAPER);
		render_rect(RENDER_BOTTOM, 4, y + ROW_H - 1, 248, 1, RENDER_LINE);
		render_pin(RENDER_BOTTOM, r->threat, 6, y, RENDER_SPR_FRONT);
		render_text(RENDER_BOTTOM, 26, y + 5, RENDER_INK, r->name);
		snprintf(buf, sizeof buf, "%ug", (unsigned)r->cost);
		render_text(RENDER_BOTTOM, 100, y + 5, RENDER_INK2, buf);
		snprintf(buf, sizeof buf, "+%u", (unsigned)r->reward);
		render_text(RENDER_BOTTOM, 136, y + 5, RENDER_ACCENT, buf);
		if (sim_front_taken((uint8_t)i)) {
			render_text(RENDER_BOTTOM, 188, y + 5, RENDER_INK2, "Taken");
		} else if (snap->treasury >= r->cost) {
			render_button(RENDER_BOTTOM, 180, y + 1, 70, 14, "Recruit",
			              f ? RENDER_BTN_FOCUS : RENDER_BTN_NORMAL);
		} else {
			snprintf(buf, sizeof buf, "Need %u",
			         (unsigned)(r->cost - snap->treasury));
			render_button(RENDER_BOTTOM, 180, y + 1, 70, 14, buf,
			              RENDER_BTN_DISABLED);
		}
		if (f) {
			render_ring(RENDER_BOTTOM, 4, y, 248, ROW_H - 1);
		}
	}
	render_icon(RENDER_BOTTOM, RENDER_ICON_GOLD, 232, 3, RENDER_SPR_FRONT);
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

	ui_draw_layout(CAMPAIGN_ROWS,
	               (uint8_t)(sizeof CAMPAIGN_ROWS / sizeof CAMPAIGN_ROWS[0]));
	paint(snap, tab);

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
		ui_draw_text(0, 12, "A recruit. Up/Down pick a front. START settings.");
	}
}

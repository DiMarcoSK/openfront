/* result.c - the payoff ledger (B10, spec-05 R1-R9).
 * One line per changed value with a signed delta and a destination hint;
 * Continue applies the sealed outcome exactly once and returns to
 * Campaign with the changed row focused; Retry restores the pre-battle
 * state and the same seed (spec-05 R6). Tick frozen here (loop run-mode
 * gate, spec-05 R1).
 * B24: Continue is the commit boundary (one core write per battle end);
 * the SaveStatus row repaints in the same frame the commit settles and
 * holds 1.5 s (or until A) before the Campaign handoff - on failure the
 * delta stays applied in memory and the row says so (spec-05 T013).
 * Art pass: the Order Sheet frame with its stamp (CONFIRMED / REPELLED)
 * below, the field the battle was fought on above.
 */
#include <nds.h>
#include <stdio.h>
#include <string.h>

#include "contracts/sim_api.h"
#include "contracts/save.h"
#include "contracts/queue.h"
#include "contracts/render.h"
#include "contracts/ui.h"

#include "ui_draw.h"
#include "screens.h"

static int focus_index;   /* 0 = Continue, 1 = Retry (defeat only)      */
static uint32_t cmd_seq;  /* monotonic per-boot idempotency key         */
static int applied_frames; /* frames since the Continue apply landed     */
static ui_screen_t request = UI_SCREEN_NONE;
#define RESULT_HOLD_FRAMES 90 /* 1.5 s: the Saved row is read, not hunted */

static bool enqueue_cmd(uint8_t cmd_id, uint8_t arg) {
	sim_cmd_t cmd;
	memset(&cmd, 0, sizeof cmd);
	cmd.seq = ++cmd_seq;
	cmd.cmd = cmd_id;
	cmd.arg = arg;
	return sim_enqueue(&cmd);
}

ui_screen_t result_request(void) {
	const ui_screen_t r = request;
	request = UI_SCREEN_NONE;
	return r;
}

void result_input(uint8_t kind, uint32_t value) {
	const battle_outcome_t *o = sim_seal_outcome();
	if (o == NULL) {
		return;
	}
	if (kind == (uint8_t)UI_EVT_TOUCH) {
		/* art pass: Continue at x 8..123, Retry at x 132..247, y 168 */
		const uint32_t px = (value >> 8) & 0xFFu;
		const uint32_t py = value & 0xFFu;
		if (py < 160u) {
			return;
		}
		focus_index = (px >= 128u && o->result == 0u) ? 1 : 0;
		value = KEY_A; /* one tap acts: both are safe actions (R5)    */
	} else if (kind != (uint8_t)UI_EVT_KEY) {
		return;
	}
	const bool defeat = (o->result == 0u);
	const int focused = defeat ? focus_index : 0;

	if (sim_outcome_applied()) {
		if (value & (KEY_A | KEY_B)) {
			request = UI_SCREEN_CAMPAIGN; /* skip the 1.5 s hold     */
		}
		return;
	}
	if (value & (KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT)) {
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

static const ui_row_t RESULT_ROWS[] = {
	{0, RENDER_BOTTOM, 14, 12, 230, 1, RENDER_INK},
	{8, RENDER_BOTTOM, 14, 132, 104, 3, RENDER_INK2},
};

/* what the battle did to the front (ADR-0009): won, held against a
 * threat, still in danger, or still the foe's                           */
static const char *front_word(const battle_outcome_t *o, bool defeat) {
	const uint8_t k = sim_battle_kind();
	const bool defence = k != SIM_WAR_CONQUEST && k != SIM_WAR_RECONQUEST;
	if (!defeat) {
		return defence ? "DEFENDED" : "TAKEN";
	}
	if (defence) {
		return sim_front_taken(o->province_id) ? "IN DANGER" : "LOST";
	}
	return "HELD";
}

static void paint(const battle_outcome_t *o, bool defeat, bool applied,
                  int focused) {
	char buf[48];
	const front_art_t *a = front_art(o->province_id);
	const sim_province_record_t *tab = sim_province_table(NULL);
	const uint32_t step = ui_anim_step();
	/* the god fought here: the front's own, or the one a wrath brought */
	const uint8_t boss = sim_battle_boss();
	const uint8_t god = (boss < SIM_BOSSES) ? boss : (uint8_t)RENDER_GOD_NONE;

	/* top: the field it was fought on, the survivors standing          */
	render_backdrop(RENDER_TOP, a->map, god);
	render_camera(130, 150);
	for (int i = 0; i < o->units_after && i < 16; ++i) {
		render_squad(RENDER_CIV_HELLENES, RENDER_CLS_SPEAR, 0u,
		             RENDER_ANIM_IDLE, step + (uint32_t)i,
		             90 - (i % 4) * 34, 50 + (i / 4) * 16, 0u);
	}
	if (defeat) {
		for (int i = 0; i < 8; ++i) {
			render_squad(a->enemy, RENDER_CLS_SPEAR, 0u, RENDER_ANIM_IDLE,
			             step + (uint32_t)i, 150 + (i % 4) * 34,
			             50 + (i / 4) * 16, RENDER_SPR_FLIP);
		}
		if (god != RENDER_GOD_NONE) {
			render_god(god, RENDER_GOD_IDLE, step, 120, 0,
			           RENDER_SPR_FLIP);
		}
	} else if (god != RENDER_GOD_NONE) {
		/* the god's fall, frozen on its last frame                  */
		render_god(god, RENDER_GOD_DEATH,
		           render_god_frames(RENDER_GOD_DEATH) - 1u, 120, 0,
		           RENDER_SPR_FLIP);
	}
	render_panel(RENDER_TOP, 8, 128, 240, 58, RENDER_PANEL_IRON);
	snprintf(buf, sizeof buf, "%s  %s", defeat ? "DEFEAT" : "VICTORY",
	         tab[o->province_id].name);
	render_text_big(RENDER_TOP, 16, 134, defeat ? RENDER_CRIT_L : RENDER_BRONZE_L,
	                defeat ? "DEFEAT" : "VICTORY");
	render_text(RENDER_TOP, 112, 138, RENDER_LINE, tab[o->province_id].name);
	save_row_paint(RENDER_TOP, SAVE_ROW_RESULT, 16, 156, 224);
	if (!defeat && god != RENDER_GOD_NONE) {
		render_relic(RENDER_TOP, god, 224, 134, RENDER_SPR_FRONT);
	}

	/* bottom: the Order Sheet                                          */
	render_backdrop(RENDER_BOTTOM, defeat ? RENDER_BD_RESULT_LOSS
	                                      : RENDER_BD_RESULT_WIN,
	                RENDER_GOD_NONE);
	{
		const unsigned s = sim_battle_time() / SIM_BATTLE_HZ;
		snprintf(buf, sizeof buf, "Day %u, %u:%02u of battle. %s",
		         (unsigned)sim_snapshot()->day, s / 60u, s % 60u,
		         sim_war_name(sim_battle_kind()));
	}
	render_text(RENDER_BOTTOM, 14, 24, RENDER_INK2, buf);
	const unsigned lost = sim_battle_losses();
	struct { uint8_t icon; const char *label; char val[16]; uint8_t col; } rows[4];
	rows[0].icon = RENDER_ICON_GOLD;
	rows[0].label = "Plunder";
	snprintf(rows[0].val, sizeof rows[0].val, "%+ld", (long)o->treasury_delta);
	rows[0].col = RENDER_INK;
	rows[1].icon = RENDER_ICON_CASUALTIES;
	rows[1].label = "Fallen";
	snprintf(rows[1].val, sizeof rows[1].val, "%s%u", lost ? "-" : "", lost);
	rows[1].col = lost ? RENDER_CRIT : RENDER_INK;
	rows[2].icon = RENDER_ICON_BATTLE;
	rows[2].label = "Front";
	snprintf(rows[2].val, sizeof rows[2].val, "%s", front_word(o, defeat));
	rows[2].col = RENDER_INK;
	rows[3].icon = RENDER_ICON_TIER;
	rows[3].label = "Front tier";
	snprintf(rows[3].val, sizeof rows[3].val, "%u > %u",
	         (unsigned)o->tier_before, (unsigned)o->tier_after);
	rows[3].col = RENDER_INK;
	for (int i = 0; i < 4; ++i) {
		const int y = 40 + i * 22;
		render_icon(RENDER_BOTTOM, rows[i].icon, 14, y, RENDER_SPR_FRONT);
		render_text(RENDER_BOTTOM, 36, y + 5, RENDER_INK, rows[i].label);
		render_text_big(RENDER_BOTTOM, 236 - (int)strlen(rows[i].val) * 12,
		                y + 2, rows[i].col, rows[i].val);
	}
	/* GAME_DESIGN 8: a god's fall pays a permanent relic - name it and
	 * what it does, so the reward is felt, not just counted           */
	{
		if (!defeat && boss != SIM_BOSS_NONE) {
			char rb[48];
			render_relic(RENDER_BOTTOM, boss, 14, 134, RENDER_SPR_FRONT);
			snprintf(rb, sizeof rb, "Relic won: %s", sim_relic_name(boss));
			render_text(RENDER_BOTTOM, 36, 135, RENDER_BRONZE_D, rb);
			render_text(RENDER_BOTTOM, 36, 147, RENDER_INK2, sim_relic_brief(boss));
		} else if (defeat && boss != SIM_BOSS_NONE) {
			render_text(RENDER_BOTTOM, 14, 137, RENDER_INK2, "Watch the omen, then answer:");
			render_text(RENDER_BOTTOM, 14, 149, RENDER_CRIT, sim_boss_omen(boss));
		}
	}
	if (applied) {
		render_button(RENDER_BOTTOM, 8, 168, 240, 20, "To Campaign (A)",
		              RENDER_BTN_FOCUS);
		return;
	}
	render_button(RENDER_BOTTOM, 8, 168, defeat ? 116 : 240, 20, "Continue",
	              focused == 0 ? RENDER_BTN_FOCUS : RENDER_BTN_NORMAL);
	if (defeat) {
		render_button(RENDER_BOTTOM, 132, 168, 116, 20, "Retry",
		              focused == 1 ? RENDER_BTN_FOCUS : RENDER_BTN_NORMAL);
	}
	render_ring(RENDER_BOTTOM, focused == 0 ? 8 : 132, 168,
	            (defeat || focused) ? 116 : 240, 20);
}

void result_frame(void) {
	char line[80];
	ui_draw_layout(RESULT_ROWS, (uint8_t)(sizeof RESULT_ROWS / sizeof RESULT_ROWS[0]));
	const battle_outcome_t *o = sim_seal_outcome();
	if (o == NULL) {
		ui_draw_text(0, 0, "RESULT: awaiting the sealed outcome");
		return;
	}
	static bool dev_line_done;
	if (!dev_line_done) {
		/* B14 (spec-05 T021): the dev trace line, harness only, no
		 * shipped telemetry; render_ms = the diag frame window's max. */
		dev_line_done = true;
		iprintf("t=result dev battle_id=%lu result=%u "
		        "delta=%ld render_ms=%lu save=%u\n",
		        (unsigned long)o->battle_id,
		        (unsigned)o->result, (long)o->treasury_delta,
		        (unsigned long)diag_stats()[DIAG_KEY_FRAME_MS].max,
		        (unsigned)save_status());
	}
	const sim_snapshot_t *snap = sim_snapshot();
	const sim_province_record_t *tab = sim_province_table(NULL);
	const bool defeat = (o->result == 0u);
	const bool applied = sim_outcome_applied();
	const int focused = defeat ? focus_index : 0;

	if (applied) {
		if (++applied_frames >= RESULT_HOLD_FRAMES) {
			request = UI_SCREEN_CAMPAIGN;
		}
	} else {
		applied_frames = 0;
	}
	paint(o, defeat, applied, focused);

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
	         (unsigned)sim_battle_losses(),
	         (unsigned)o->units_before);
	ui_draw_text(0, 4, line);
	snprintf(line, sizeof line, "FRONT %-11s (Campaign)  %s",
	         tab[o->province_id].name, front_word(o, defeat));
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
	{
		/* B24 (spec-05 T013): the status row after the commit; on
		 * failure the delta stays applied and the row says so.      */
		char l1[64], l2[64];
		save_row(SAVE_ROW_RESULT, l1, l2, sizeof l1);
		ui_draw_text(0, 13, l1);
		ui_draw_text(0, 17, l2);
	}
	ui_draw_text(0, 14, "TICK PAUSED");
	if (defeat) {
		snprintf(line, sizeof line, "[ RETRY same seed ]%s",
		         (focused == 1) ? "<" : "");
		ui_draw_text(0, 15, line);
	}
}

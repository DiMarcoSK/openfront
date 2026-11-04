/* settings.c - the Settings / Save block (B24, spec-06 T019-T022, T027).
 * The owner of every save action: the status row, the 2 slot lines,
 * Retry save (the only way out of FULL, R7), Erase with the 5 s Undo
 * (R6: both 340 B images held in RAM, no modal) and the 3-line save
 * diagnostics disclosure. Opened by START from any host screen (or the
 * title's Settings button); B returns to the host it came from. Focus on
 * arrival: Back when OK, Retry on NO_CARD/FULL, Erase on VERIFY_FAIL.
 */
#include <nds.h>
#include <stdio.h>
#include <string.h>

#include "contracts/save.h"
#include "contracts/render.h"
#include "contracts/ui.h"

#include "ui_draw.h"
#include "screens.h"

enum { ACT_RETRY = 0, ACT_ERASE, ACT_DIAG, ACT_BACK, ACTS };

static ui_screen_t host = UI_SCREEN_TITLE;
static ui_screen_t request = UI_SCREEN_NONE;
static int focus = ACT_BACK;
static bool diag_open;
static int undo_frames;         /* > 0 while "Undo 5 s" runs            */
static char note[64];

static bool erase_possible(void) {
	save_slot_info_t s[2];
	save_slots(s);
	return save_status() != SAVE_STATUS_NO_CARD && !(s[0].blank && s[1].blank);
}

static bool enabled(int act) {
	if (undo_frames > 0) {
		return act == ACT_ERASE; /* the Undo is the one live action  */
	}
	switch (act) {
	case ACT_RETRY: return save_status() != SAVE_STATUS_OK;
	case ACT_ERASE: return erase_possible();
	default: return true;
	}
}

void settings_open(ui_screen_t from) {
	host = (from == UI_SCREEN_SETTINGS || from == UI_SCREEN_NONE)
	           ? UI_SCREEN_TITLE
	           : from;
	request = UI_SCREEN_NONE;
	diag_open = false;
	note[0] = '\0';
	switch (save_status()) {
	case SAVE_STATUS_NO_CARD:
	case SAVE_STATUS_FULL: focus = ACT_RETRY; break;
	case SAVE_STATUS_VERIFY_FAIL: focus = ACT_ERASE; break;
	default: focus = ACT_BACK; break;
	}
	ui_set_screen(UI_SCREEN_SETTINGS);
}

ui_screen_t settings_request(void) {
	const ui_screen_t r = request;
	request = UI_SCREEN_NONE;
	return r;
}

static void act(int a) {
	if (!enabled(a)) {
		snprintf(note, sizeof note, "%s",
		         a == ACT_RETRY ? "Save is fine. Nothing to retry."
		                        : "Nothing to erase.");
		return;
	}
	switch (a) {
	case ACT_RETRY: {
		const save_status_t s = save_retry();
		snprintf(note, sizeof note, "%s",
		         s == SAVE_STATUS_OK ? "Retry saved." : "Retry failed. Input kept.");
		break;
	}
	case ACT_ERASE:
		if (undo_frames > 0) {
			save_erase_undo();
			undo_frames = 0;
			snprintf(note, sizeof note, "Undo: both slots restored.");
		} else {
			save_erase();
			undo_frames = (int)SAVE_UNDO_FRAMES;
			note[0] = '\0';
		}
		break;
	case ACT_DIAG:
		diag_open = !diag_open;
		break;
	default:
		request = host;
		break;
	}
}

void settings_input(uint8_t kind, uint32_t value) {
	if (kind == (uint8_t)UI_EVT_TOUCH) {
		/* the 4 buttons: 2 x 2 grid of 116 x 24 at y 104 / 132        */
		const uint32_t px = (value >> 8) & 0xFFu;
		const uint32_t py = value & 0xFFu;
		if (py < 100u || py >= 160u) {
			return;
		}
		const int t = ((py >= 130u) ? 2 : 0) + ((px >= 128u) ? 1 : 0);
		if (t != focus) {
			focus = t; /* tap 1 rings                                 */
			return;
		}
		act(t);
		return;
	}
	if (kind != (uint8_t)UI_EVT_KEY) {
		return;
	}
	if (value & KEY_B) {
		if (undo_frames == 0) {
			request = host;
		}
		return;
	}
	if (value & KEY_RIGHT) {
		focus = (focus + 1) % ACTS;
	} else if (value & KEY_LEFT) {
		focus = (focus + ACTS - 1) % ACTS;
	} else if (value & (KEY_UP | KEY_DOWN)) {
		focus = (focus + 2) % ACTS;
	} else if (value & KEY_A) {
		act(undo_frames > 0 ? ACT_ERASE : focus);
	}
}

static const ui_row_t SETTINGS_ROWS[] = {
	{0, RENDER_BOTTOM, 8, 8, 244, 1, RENDER_BRONZE_L},
	{5, RENDER_BOTTOM, 14, 60, 230, 1, RENDER_INK},
	{6, RENDER_BOTTOM, 14, 70, 230, 1, RENDER_INK},
	{7, RENDER_BOTTOM, 14, 84, 230, 1, RENDER_ACCENT},
	{12, RENDER_BOTTOM, 14, 160, 230, 1, RENDER_INK2},
	{13, RENDER_BOTTOM, 14, 169, 230, 1, RENDER_INK2},
	{14, RENDER_BOTTOM, 14, 178, 230, 1, RENDER_INK2},
};

static void slot_line(int i, char *buf, size_t n) {
	save_slot_info_t s[2];
	save_slots(s);
	if (s[i].valid) {
		snprintf(buf, n, "Slot %d: seq %lu, Day %u, 170 B, CRC ok", i + 1,
		         (unsigned long)s[i].seq, (unsigned)s[i].day);
	} else if (s[i].blank) {
		snprintf(buf, n, "Slot %d: blank, 0 of 170 B", i + 1);
	} else {
		snprintf(buf, n, "Slot %d: damaged, CRC fail, kept", i + 1);
	}
}

void settings_frame(void) {
	char buf[64];
	ui_draw_layout(SETTINGS_ROWS, (uint8_t)(sizeof SETTINGS_ROWS / sizeof SETTINGS_ROWS[0]));
	if (undo_frames > 0) {
		if (--undo_frames == 0) {
			save_erase_settle(); /* 5 s out: the slots stay blank  */
			snprintf(note, sizeof note, "Erase settled. New Campaign next.");
		}
	}

	render_backdrop(RENDER_TOP, RENDER_BD_TITLE_TOP, RENDER_GOD_NONE);
	render_backdrop(RENDER_BOTTOM, RENDER_BD_FRAME_SHEET, RENDER_GOD_NONE);
	render_panel(RENDER_TOP, 8, 4, 240, 30, RENDER_PANEL_IRON);
	render_text(RENDER_TOP, 16, 15, RENDER_BRONZE_L, "SETTINGS  -  SAVE BLOCK");

	ui_draw_text(0, 0, "SETTINGS / SAVE");
	render_panel(RENDER_BOTTOM, 6, 26, 244, 70, RENDER_PANEL_SHEET);
	{
		char l1[64], l2[64];
		save_row(SAVE_ROW_SETTINGS, l1, l2, sizeof l1);
		if (undo_frames > 0) {
			/* "Erased 340 B. Undo 5 s." byte-exact at the start,
			 * counting down per whole second (spec-06 6.3 B).     */
			snprintf(l1, sizeof l1, "Erased %u B. Undo %d s.",
			         (unsigned)SAVE_CORE_BYTES, (undo_frames + 59) / 60);
		}
		ui_draw_text(0, 2, l1);
		ui_draw_text(0, 3, l2);
		save_row_paint(RENDER_BOTTOM, SAVE_ROW_SETTINGS, 14, 32, 230);
		if (undo_frames > 0) {
			/* the countdown overrides the painted line 1 too      */
			render_rect(RENDER_BOTTOM, 34, 32, 210, 8, RENDER_SURFACE);
			render_text(RENDER_BOTTOM, 34, 32, RENDER_CRIT, l1);
		}
	}
	slot_line(0, buf, sizeof buf);
	ui_draw_text(0, 5, buf);
	slot_line(1, buf, sizeof buf);
	ui_draw_text(0, 6, buf);
	ui_draw_text(0, 7, note);

	static const char *const LABEL[ACTS] = {"Retry save", "Erase", "Diagnostics", "Back"};
	for (int i = 0; i < ACTS; ++i) {
		const int x = 6 + (i & 1) * 126, y = 104 + (i >> 1) * 28;
		const char *label = (i == ACT_ERASE && undo_frames > 0) ? "Undo (A)" : LABEL[i];
		render_button(RENDER_BOTTOM, x, y, 118, 24, label,
		              !enabled(i) ? RENDER_BTN_DISABLED
		              : focus == i ? RENDER_BTN_FOCUS : RENDER_BTN_NORMAL);
		if (focus == i) {
			render_ring(RENDER_BOTTOM, x, y, 118, 24);
		}
		snprintf(buf, sizeof buf, "[%s]%s%s", label, enabled(i) ? "" : " off",
		         focus == i ? "<" : "");
		ui_draw_text(0, 8 + i, buf);
	}
	if (diag_open) {
		save_slot_info_t s[2];
		save_slots(s);
		snprintf(buf, sizeof buf, "commit seq %lu",
		         (unsigned long)save_commit_seq());
		ui_draw_text(0, 12, buf);
		snprintf(buf, sizeof buf, "slot seqs %lu | %lu",
		         (unsigned long)s[0].seq, (unsigned long)s[1].seq);
		ui_draw_text(0, 13, buf);
		snprintf(buf, sizeof buf, "last status %s", save_status_text(save_status()));
		ui_draw_text(0, 14, buf);
	} else {
		ui_draw_text(0, 12, "A act. B back. Diagnostics: 3 lines.");
	}
}

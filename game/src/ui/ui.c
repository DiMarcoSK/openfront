/* ui.c - the 7-screen shell (module: ui).
 * B01 scaffold, extended by B07: input enters per event through the
 * conduit, the title owns its state machine (title.c) and the handoff
 * switch lives here because loop owns the screen switches (ui.h).
 * The remaining screens ship with B08/B09/B10/B12/B24.
 */
#include <nds.h>
#include <stdio.h>
#include <string.h>

#include "ui_draw.h"
#include "title.h"

#include "contracts/ui.h"
#include "contracts/queue.h"
#include "contracts/sim_api.h"
#include "contracts/save.h"
#include "contracts/audio.h"

void catalog_render(void);

static ui_screen_t current = UI_SCREEN_NONE;
static bool catalog_open;
static bool title_inited;

/* spec-01 6.6 T10, drawn on the Campaign header (never as a title toast). */
static char campaign_note[64];

void ui_set_screen(ui_screen_t screen) {
	current = screen;
	if (current == UI_SCREEN_TITLE && !title_inited) {
		/* The title enters in LOADING once; the card scan runs inside
		 * the first title frames (spec-01 R8, boot.c drives no scan). */
		title_inited = true;
		title_init();
	}
}

ui_screen_t ui_screen(void) {
	return current;
}

void ui_input(uint8_t kind, uint32_t value) {
	if (current == UI_SCREEN_TITLE) {
		title_input(kind, value);
		return;
	}
	/* Campaign/Battle/Base input mapping ships with B08/B09/B10/B12. */
}

/* handoff_to_campaign(): title confirmed a load; the switch belongs here
 * because spec-01 6.1 exits: Campaign via CONTINUE (spec-01 R2).         */
static void handoff_to_campaign(void) {
	const int slot = title_entered_slot();
	if (slot >= 0) {
		save_slot_info_t info[2];
		save_slots(info);
		snprintf(campaign_note, sizeof campaign_note,
		         "SLOT %u LOADED. DAY %u, TREASURY %lu.",
		         (unsigned)(slot + 1u), (unsigned)info[slot].day,
		         (unsigned long)info[slot].treasury);
	} else {
		/* New Campaign: Day 1 / treasury 300 is the sim default (spec-01). */
		snprintf(campaign_note, sizeof campaign_note, "DAY 1.");
	}
	ui_set_screen(UI_SCREEN_CAMPAIGN);
}

static void draw_campaign(void) {
	char line[80];
	const sim_snapshot_t *snap = sim_snapshot();
	ui_draw_text(0, 0, "WAR TABLE - CAMPAIGN");
	snprintf(line, sizeof line, "DAY %u        TREASURY %lu",
	         (unsigned)snap->day, (unsigned long)snap->treasury);
	ui_draw_text(0, 1, line);
	ui_draw_text(0, 3, campaign_note);
	ui_draw_text(0, 5, "ECONOMY TICKING (+2 PER 600-TICK DAY AT B08)");
	for (int i = 6; i < 18; ++i) {
		ui_draw_text(0, i, "");
	}
}

void ui_frame(void) {
	if (catalog_open) {
		catalog_render();
		return;
	}
	if (current == UI_SCREEN_NONE) {
		return;
	}
	if (current == UI_SCREEN_TITLE) {
		title_frame();
		if (title_state() == TITLE_HANDOFF) {
			handoff_to_campaign();
		}
		return;
	}
	if (current == UI_SCREEN_CAMPAIGN) {
		draw_campaign();
		return;
	}
	for (int i = 0; i < 18; ++i) {
		ui_draw_text(0, i, "");
	}
}

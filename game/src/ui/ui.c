/* ui.c - the 7-screen shell (module: ui).
 * B01 scaffold, extended by B07 (title machine) and B08/B09/B10 (campaign,
 * base, battle, result). One frame per screen; every screen switch happens
 * here ("loop owns switches", ui.h v1). Screens emit commands through the
 * conduit and never write run state (constitution 6).
 */
#include <nds.h>
#include <stdio.h>
#include <string.h>

#include "ui_draw.h"
#include "title.h"
#include "screens.h"

#include "contracts/ui.h"
#include "contracts/queue.h"
#include "contracts/sim_api.h"
#include "contracts/save.h"

void catalog_render(void);

static ui_screen_t current = UI_SCREEN_NONE;
static bool catalog_open;
static bool title_inited;

void ui_set_screen(ui_screen_t screen) {
	current = screen;
	if (current == UI_SCREEN_TITLE && !title_inited) {
		/* The title enters in LOADING once; the card scan runs inside
		 * the first title frames (spec-01 R8; boot.c drives no scan). */
		title_inited = true;
		title_init();
	}
}

ui_screen_t ui_screen(void) {
	return current;
}

void ui_input(uint8_t kind, uint32_t value) {
	switch (current) {
	case UI_SCREEN_TITLE:
		title_input(kind, value);
		return;
	case UI_SCREEN_CAMPAIGN:
		campaign_input(kind, value);
		return;
	case UI_SCREEN_BASE:
		base_input(kind, value);
		return;
	case UI_SCREEN_BATTLE:
		battle_input(kind, value);
		return;
	case UI_SCREEN_RESULT:
		result_input(kind, value);
		return;
	default:
		break; /* Settings content ships with B24 */
	}
}

/* handoff_to_campaign(): standard entry into Campaign: the changed row
 * focus follows an applied outcome (spec-05 R4), a B07 load falls back to
 * row 0.                                                                 */
static void enter_campaign(void) {
	const uint8_t changed = sim_changed_province();
	if (changed != 0xFFu) {
		campaign_focus_province(changed);
		sim_clear_changed_province();
	}
	ui_set_screen(UI_SCREEN_CAMPAIGN);
}

static void shell_title_handoff(void) {
	enter_campaign();
}

void ui_frame(void) {
	if (catalog_open) {
		catalog_render();
		return;
	}
	if (current == UI_SCREEN_NONE) {
		return;
	}

	switch (current) {
	case UI_SCREEN_TITLE:
		title_frame();
		if (title_state() == TITLE_HANDOFF) {
			shell_title_handoff(); /* spec-01 R2 handoff */
		}
		return;

	case UI_SCREEN_CAMPAIGN:
		campaign_frame();
		{
			const ui_screen_t r = campaign_request();
			if (r != UI_SCREEN_NONE) {
				ui_set_screen(r);
			}
		}
		return;

	case UI_SCREEN_BASE:
		base_frame();
		{
			const ui_screen_t r = base_request();
			if (r != UI_SCREEN_NONE) {
				/* The Base exit is the coalesced save boundary
				 * (spec-02 T012); the write lands in Campaign. */
				ui_set_screen(r);
			}
		}
		return;

	case UI_SCREEN_BATTLE:
		if (sim_battle_sealed()) {
			ui_set_screen(UI_SCREEN_RESULT); /* spec-05 R1: <= 1 frame */
			return;
		}
		battle_frame();
		return;

	case UI_SCREEN_RESULT:
		result_frame();
		if (sim_outcome_applied()) {
			/* Continue applied: back to Campaign, changed row
			 * focused (spec-05 R4).                                */
			enter_campaign();
		} else if (!sim_battle_sealed() && sim_battle_active()) {
			/* Retry accepted: back to the live battle (R6).        */
			ui_set_screen(UI_SCREEN_BATTLE);
		}
		return;

	default:
		break; /* Settings: B24 */
	}
}

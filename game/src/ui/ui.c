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
#include "overlay.h"
#include "screens.h"

#include "contracts/ui.h"
#include "contracts/queue.h"
#include "contracts/sim_api.h"
#include "contracts/save.h"

void catalog_render(void);

static ui_screen_t current = UI_SCREEN_NONE;
static bool catalog_open;
static bool title_inited;

/* Golden frame hash (B05): FNV-1a over the state that a frame renders.
 * Both emulators print the same line to stdout; B31's pixel captures
 * extend this to art. Equal across DeSmuME and MelonDS by construction.  */
static uint32_t frame_hash;

static uint32_t hash_bytes(uint32_t h, const uint8_t *p, uint32_t n) {
	for (uint32_t i = 0; i < n; ++i) {
		h ^= p[i];
		h *= 0x01000193u;
	}
	return h;
}

static void bench_frame_hash(void) {
	uint8_t buf[32];
	const sim_snapshot_t *snap = sim_snapshot();
	frame_hash = 0x811C9DC5u;
	buf[0] = (uint8_t)current;
	buf[1] = (uint8_t)save_status();
	frame_hash = hash_bytes(frame_hash, buf, 2);
	if (snap) {
		frame_hash = hash_bytes(frame_hash,
		                        (const uint8_t *)snap,
		                        sizeof *snap);
	}
	frame_hash = hash_bytes(frame_hash, sim_formation(),
	                        SIM_FORMATION_SLOTS);
	buf[0] = (uint8_t)sim_battle_active();
	buf[1] = sim_battle_turn();
	buf[2] = sim_battle_enemies();
	buf[3] = sim_battle_units();
	buf[4] = sim_battle_province();
	frame_hash = hash_bytes(frame_hash, buf, 5);
	frame_hash = hash_bytes(frame_hash, sim_front_taken(0u) ?
	                        (const uint8_t *)"\x01" : (const uint8_t *)"\x00", 1u);
	/* every ~0.5 s: one line, both emulators' console fold onto it     */
	static uint32_t ticks_since_print;
	if ((current == UI_SCREEN_BATTLE) &&
	    (++ticks_since_print >= 30u)) {
		ticks_since_print = 0u;
		iprintf("t=bench frame hash %08lx\n",
		        (unsigned long)frame_hash);
	}
}

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

/* B13 (spec-02 T033): the row the coalesced commit should persist;
 * 0xFF when no list screen owns a selection. Additive to ui.h v1.        */
uint8_t ui_selected_row(void) {
	return (current == UI_SCREEN_CAMPAIGN) ? campaign_focus_row() : 0xFFu;
}

bool ui_sim_hold(void) {
	return current == UI_SCREEN_BATTLE && battle_holds();
}

void ui_input(uint8_t kind, uint32_t value) {
	/* B24: START opens Settings from any host screen (spec-06 6.4);
	 * the title routes its own Settings action below.                  */
	if (kind == (uint8_t)UI_EVT_KEY && (value & KEY_START) &&
	    current != UI_SCREEN_TITLE && current != UI_SCREEN_SETTINGS) {
		settings_open(current);
		return;
	}
	switch (current) {
	case UI_SCREEN_TITLE:
		if (title_input(kind, value) == TITLE_ACT_SETTINGS) {
			settings_open(UI_SCREEN_TITLE);
		}
		return;
	case UI_SCREEN_SETTINGS:
		settings_input(kind, value);
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
		break;
	}
}

/* handoff_to_campaign(): standard entry into Campaign: the changed row
 * focus follows an applied outcome (spec-05 R4); otherwise the focus
 * restores the persisted selection byte 62 (B13 T033: reboot puts the
 * saved row back, out-of-range fell back to row 0 at ingest).            */
static void enter_campaign(void) {
	const uint8_t changed = sim_changed_province();
	if (changed != 0xFFu) {
		campaign_focus_province(changed);
		sim_clear_changed_province();
	} else {
		campaign_focus_province(sim_selected_province());
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
	bench_frame_hash();
	ui_anim_tick();

	switch (current) {
	case UI_SCREEN_TITLE:
		title_frame();
		if (title_state() == TITLE_HANDOFF) {
			shell_title_handoff(); /* spec-01 R2 handoff */
		}
		overlay_frame();
		return;

	case UI_SCREEN_CAMPAIGN:
		campaign_frame();
		{
			const ui_screen_t r = campaign_request();
			if (r != UI_SCREEN_NONE) {
				ui_set_screen(r);
			}
		}
		overlay_frame();
		return;

	case UI_SCREEN_BASE:
		base_frame();
		{
			const ui_screen_t r = base_request();
			if (r != UI_SCREEN_NONE) {
				/* The Base exit is the coalesced save boundary
				 * (spec-02 T012); the write lands in Campaign. */
				if (r == UI_SCREEN_BATTLE) {
					battle_enter(true); /* boss intro once */
				}
				ui_set_screen(r);
			}
		}
		overlay_frame();
		return;

	case UI_SCREEN_BATTLE:
		if (sim_battle_sealed()) {
			ui_set_screen(UI_SCREEN_RESULT); /* spec-05 R1: <= 1 frame */
			return;
		}
		battle_frame();
		overlay_frame();
		return;

	case UI_SCREEN_RESULT:
		result_frame();
		if (result_request() == UI_SCREEN_CAMPAIGN) {
			/* Continue applied + its commit row read: back to
			 * Campaign, changed row focused (spec-05 R4).          */
			enter_campaign();
		} else if (!sim_battle_sealed() && sim_battle_active()) {
			/* Retry accepted: back to the live battle (R6).        */
			battle_enter(false);
			ui_set_screen(UI_SCREEN_BATTLE);
		}
		overlay_frame();
		return;

	case UI_SCREEN_SETTINGS:
		settings_frame();
		{
			const ui_screen_t r = settings_request();
			if (r == UI_SCREEN_TITLE) {
				title_refresh(); /* an Erase / Retry re-reads */
				ui_set_screen(r);
			} else if (r == UI_SCREEN_CAMPAIGN) {
				enter_campaign();
			} else if (r != UI_SCREEN_NONE) {
				ui_set_screen(r);
			}
		}
		overlay_frame();
		return;

	default:
		overlay_frame();
		break;
	}
}

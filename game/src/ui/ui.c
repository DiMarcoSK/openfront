/* ui.c - the 7-screen shell (module: ui).
 * B01 scaffold: the title screen. The full 7-screen state machine ships
 * with B07/B08/B09/B10/B11/B12/B13/B24 (one screen per backlog item).
 */
#include <nds.h>
#include <stdio.h>

#include "ui_draw.h"
#include <string.h>

#include "contracts/ui.h"
#include "contracts/queue.h"
#include "contracts/sim_api.h"
#include "contracts/save.h"
#include "contracts/audio.h"

void catalog_render(void);
void catalog_render_l_r(void);

static ui_screen_t current = UI_SCREEN_NONE;
static bool catalog_open;

void ui_set_screen(ui_screen_t screen) {
	current = screen;
}

ui_screen_t ui_screen(void) {
	return current;
}

void ui_toggle_catalog(void) {
	catalog_open = !catalog_open;
}

void ui_frame(void) {
	if (catalog_open) {
		catalog_render();
		return;
	}
	if (current == UI_SCREEN_NONE) {
		return;
	}
	const sim_snapshot_t *snap = sim_snapshot();
	ui_draw_text(0, 0, "WAR TABLE        DAY 1        TREASURY 300  +2/DAY");
	ui_draw_text(0, 1, "SLOT 1   [damaged]    DAY -   treasury -");
	ui_draw_text(0, 2, "SLOT 2   [Continue]   DAY -   treasury -    focus");
	const char *st = save_status_text(save_status());
	ui_draw_text(0, 5, st);
	for (int i = 6; i < 18; ++i) {
		ui_draw_text(0, i, "");
	}
}

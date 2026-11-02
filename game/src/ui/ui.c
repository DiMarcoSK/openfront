/* ui.c - the 7-screen shell (module: ui).
 * B01 scaffold: renders the title screen name so users see something other
 * than the console demo; the title state machine ships with B07/B11.
 */
#include <nds.h>
#include <calico.h>
#include <stdio.h>

#include "contracts/ui.h"
#include "contracts/sim_api.h"
#include "contracts/save.h"

static ui_screen_t current = UI_SCREEN_NONE;

void ui_set_screen(ui_screen_t screen) {
	current = screen;
}

ui_screen_t ui_screen(void) {
	return current;
}

void ui_frame(void) {
	if (current == UI_SCREEN_NONE) {
		return;
	}
	const sim_snapshot_t *snap = sim_snapshot();
	(void)snap;
	iprintf("\x1b[1;1H%s", "WAR TABLE");
	const char *status = save_status_text(save_status());
	iprintf("\x1b[3;1Hsave: %-18s", status);
}

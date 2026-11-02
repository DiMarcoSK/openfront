/* battle.c - top battlefield + bottom 1-row strip (B10, spec-04 R1-R3).
 * Walking skeleton: auto-resolve only - the order tiles, speed badge and
 * Undo chip land with B20/B21/B22; the strip carries the read fields that
 * exist at B10 (front, turn, chips, HP). The field is text chips until
 * the B27/B31 pixel pass; the sealed frame is never redrawn (R11).
 */
#include <nds.h>
#include <stdio.h>
#include <string.h>

#include "contracts/sim_api.h"
#include "contracts/queue.h"
#include "contracts/ui.h"

#include "ui_draw.h"
#include "screens.h"

void battle_input(uint8_t kind, uint32_t value) {
	if (kind == (uint8_t)UI_EVT_KEY && (value & KEY_A)) {
		/* orders land with B21; the strip keeps answering (input live) */
	}
}

void battle_frame(void) {
	char line[80];
	if (!sim_battle_active() && !sim_battle_sealed()) {
		ui_draw_text(0, 0, "FRONT: no battle running");
		return;
	}
	const sim_snapshot_t *snap = sim_snapshot();
	const sim_province_record_t *tab = sim_province_table(NULL);

	snprintf(line, sizeof line, "T%u/9  FRONT %s  ETC T%u",
	         (unsigned)sim_battle_turn(), tab[sim_battle_province()].name,
	         (unsigned)(SIM_BATTLE_TURNS - sim_battle_turn()));
	ui_draw_text(0, 0, line);

	/* field: 6 chip rows stand in for the 64 OAM units (text fidelity)   */
	const uint8_t chips = sim_battle_units();
	uint8_t drawn = 0u;
	for (int r = 0; r < 6; ++r) {
		size_t used = 0u;
		line[0] = '\0';
		for (int c = 0; c < 10 && used < sizeof line - 4u; ++c) {
			if (drawn < chips) {
				drawn++;
				used += (size_t)snprintf(line + used,
				                         sizeof line - used, "[=]");
			} else {
				used += (size_t)snprintf(line + used,
				                         sizeof line - used, "...");
			}
		}
		ui_draw_text(0, 2 + r, line);
	}

	/* the 1-row strip (<= 32 px; spec-04 R1): mm | turn | ETC | HP       */
	snprintf(line, sizeof line, "|mm|T%u ETC %u|HP %u/%u|x1|q %u/32|",
	         (unsigned)sim_battle_turn(),
	         (unsigned)sim_battle_enemies(),
	         (unsigned)sim_battle_units(),
	         (unsigned)sim_battle_units_start(),
	         (unsigned)queue_diag()->cmd_depth);
	ui_draw_text(0, 11, line);
	ui_draw_text(0, 13, "orders + speed + undo land with B20/B21");
	snprintf(line, sizeof line, "TREASURY %lu",
	         (unsigned long)snap->treasury);
	ui_draw_text(0, 15, line);
}

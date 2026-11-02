/* components.c - the 8 base components with 4 written states (B04).
 * Every component is a primitive the screens reuse; the transition states
 * come from tokens.h, not from hard-coded counters. 1x/2x captures land
 * with B31's harness; the acceptance here is the on-screen runner.
 */
#include <stdio.h>
#include <string.h>

#include "contracts/tokens.h"
#include "contracts/ui.h"
#include "contracts/sim_api.h"
#include "contracts/save.h"

#include "ui_draw.h"

static char line[96];

/* One card = slot preview: 2 px focus ring inset on focus, 1 px line on
 * disabled, 1 px crit border on crit (spec-01 6.7). Sizes stay
 * 128 x 32 in every state so muscle memory holds.                        */
void component_card(int x, int y, int st, const char *label) {
	switch (st) {
	case 0: ui_draw_text(x, y, label);                                  break;
	case 1: snprintf(line, sizeof line, "> %s <", label);
		ui_draw_text(x, y, line);                              break;
	case 2: snprintf(line, sizeof line, "%s", label);
		ui_draw_text(x, y, line);                              break;
	case 3: snprintf(line, sizeof line, "! %s !", label);
		ui_draw_text(x, y, line);                              break;
	}
}

/* Primary button = the one action of the state (spec-01 6.3).
 * Disabled style = ink-2 + 1 px dashed edge, never opacity.              */
void component_button_primary(int x, int y, int st) {
	const char *label = st == 3 ? "DIALOG" : "CONFIRM";
	switch (st) {
	case 1: snprintf(line, sizeof line, "[%s]", label); break;
	case 2: snprintf(line, sizeof line, " %s ------", label); break;
	default: snprintf(line, sizeof line, "%s", label); break;
	}
	ui_draw_text(x, y, line);
}

/* Secondary button = Settings entry; never competes with primary.        */
void component_button_secondary(int x, int y, int st) {
	const char *label = st == 3 ? "??" : "SETTINGS";
	switch (st) {
	case 1: snprintf(line, sizeof line, "(%s)", label); break;
	case 2: snprintf(line, sizeof line, "-%s-", label); break;
	default: snprintf(line, sizeof line, "%s", label); break;
	}
	ui_draw_text(x, y, line);
}

/* Ledger row: tabular mono right-aligned; a 10-digit treasury fits.      */
void component_ledger_row(int x, int y, const uint32_t val, int st) {
	if (st == 2) {
		snprintf(line, sizeof line, "TREASURY ----");
	} else {
		snprintf(line, sizeof line, "TREASURY %10lu",
			(unsigned long)val);
	}
	ui_draw_text(x, y, line);
}

/* 5) focused-slot sheet (spec-01 6.2, top panel, 224 x 88).             */
void component_sheet(int x, int y, int st, const char *slot,
                     const char *day, const char *treasure) {
	switch (st) {
	case 0:
		snprintf(line, sizeof line, "SHEET %s %s %s", slot, day,
			treasure);
		break;
	case 1:
		snprintf(line, sizeof line, "> SHEET %s %s", slot, day);
		break;
	case 2:
		snprintf(line, sizeof line, "SHEET %s ---", slot);
		break;
	default:
		snprintf(line, sizeof line, "! SHEET %s %s", slot, day);
		break;
	}
	ui_draw_text(x, y, line);
	(void)treasure;
}

/* 6) tier band (spec-03 6.6, 24 px, numeral 0..9).                      */
void component_tier_band(int x, int y, int st, int tier) {
	char num[4];
	snprintf(num, sizeof num, "%d", tier % 10);
	switch (st) {
	case 1: snprintf(line, sizeof line, "BAND [%-2s]", num); break;
	case 2: snprintf(line, sizeof line, "BAND -%-2s", num); break;
	default: snprintf(line, sizeof line, "BAND  %-2s", num); break;
	}
	ui_draw_text(x, y, line);
}

/* 7) threat pin (spec-02 6.2, 8x8 label + pin).                         */
void component_threat_pin(int x, int y, int st, const char *word) {
	switch (st) {
	case 3: snprintf(line, sizeof line, "PIN >%-4s<", word); break;
	default: snprintf(line, sizeof line, "PIN %-4s", word); break;
	}
	ui_draw_text(x, y, line);
}

/* 8) focus ring (spec-01 6.3, 2 px inset, one shape not a colour swap).  */
void component_focus_ring(int x, int y, int st) {
	switch (st) {
	case 0: ui_draw_text(x, y, "ring   off"); break;
	case 1: ui_draw_text(x, y, "ring   >..<"); break;
	case 2: ui_draw_text(x, y, "ring   ----"); break;
	default: ui_draw_text(x, y, "ring  !crit"); break;
	}
}

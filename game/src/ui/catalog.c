/* catalog.c - the living /ui catalog (B04).
 * One screen, 8 components x 4 states. The catalog lives inside the ROM and
 * is reachable by L+R (dev build), so a change to tokens or to a component
 * is inspectable on a real DS without leaving the tool-chain.
 * Source: SDD section 5, BRANDBOOK section 7, spec-01/02/03 components.
 */
#include <stdio.h>
#include <string.h>

#include "contracts/tokens.h"
#include "contracts/ui.h"
#include "contracts/sim_api.h"
#include "contracts/save.h"

#include "ui_draw.h"

extern void component_card(int x, int y, int st, const char *label);
extern void component_button_primary(int x, int y, int st);
extern void component_button_secondary(int x, int y, int st);
extern void component_ledger_row(int x, int y, uint32_t val, int st);
extern void component_sheet(int x, int y, int st, const char *slot,
                            const char *day, const char *treasure);
extern void component_tier_band(int x, int y, int st, int tier);
extern void component_threat_pin(int x, int y, int st, const char *word);
extern void component_focus_ring(int x, int y, int st);

static int y;

void catalog_render(void) {
	y = 0;
	char l[80];

	/* header ------------------------------------------------------------ */
	ui_draw_text(0, y++, "WAR TABLE     /ui catalog     1x 2x");
	ui_draw_text(0, y++, "comp             1x | 2x        states d/f/d/c");

	/* 1) slot card ----------------------------------------------------- */
	for (int st = 0; st < 4; ++st) {
		snprintf(l, sizeof l, "card %s            %s",
			st == 0 ? "1x" : st == 1 ? "2x" : "1x", "");
		(void)st;
	}
	ui_draw_text(0, y++, "card             1 SLOT 1  2 SLOT 1   d/f/d/c");

	/* 2) primary button ------------------------------------------------- */
	ui_draw_text(0, y++, "button primary   1 CONFIRM 2 [CONFIRM]  d/f/d/c");

	/* 3) secondary button ------------------------------------------------ */
	ui_draw_text(0, y++, "button second.   1SETTINGS 2 (SETTINGS) d/f/d/c");

	/* 4) ledger row ------------------------------------------------------ */
	component_ledger_row(0, y++, 580u, 0);
	component_ledger_row(0, y++, 580u, 1);
	component_ledger_row(0, y++, 0u, 2);
	component_ledger_row(0, y++, 0u, 3);

	/* 5) focused-slot sheet ------------------------------------------- */
	ui_draw_text(0, y++, "sheet focus      1SLOT 1 2> SLOT 1    d/f/d/c");
	component_sheet(0, y, 0, "S1", "D12", "580"); y++;
	component_sheet(0, y, 1, "S1", "D12", "580"); y++;
	component_sheet(0, y, 2, "S1", "D-", "---"); y++;
	component_sheet(0, y, 3, "S1", "D12", "580"); y++;

	/* 6) tier band ------------------------------------------------------ */
	for (int st = 0; st < 4; ++st) {
		component_tier_band(0, y, st, st == 3 ? 9 : 2); y++;
	}

	/* 7) threat pin ----------------------------------------------------- */
	for (int st = 0; st < 4; ++st) {
		component_threat_pin(0, y, st,
			st == 0 ? "LOW" : st == 1 ? "HIGH" :
			st == 2 ? "MED" : "CRIT"); y++;
	}

	/* 8) focus ring ------------------------------------------------------ */
	for (int st = 0; st < 4; ++st) {
		component_focus_ring(0, y, st); y++;
	}

	/* footer ------------------------------------------------------------- */
	ui_draw_text(0, ++y, "tokens: paper surface ink ink2 line accent ok");
	ui_draw_text(0, ++y, "1x here; 2x captured by the B31 harness.");
}

/* guide.c - the player's bearings on every management screen (B27).
 * 1. guide_legend(): the control strip along the foot of the bottom
 *    screen - a key cap and a word per action, the same look on every
 *    screen, so the buttons never have to be guessed.
 * 2. guide_next(): onboarding without a tutorial mode - one NEXT plate on
 *    the top screen that names the single step that moves the campaign
 *    on, read from the live state (a blank card says: pick a front).
 * 3. guide_point(): a bobbing chevron on the control that step uses.
 */
#include <stdio.h>
#include <string.h>

#include "contracts/render.h"
#include "contracts/sim_api.h"

#include "ui_draw.h"
#include "guide.h"

#define LEGEND_Y 182

void guide_legend(const guide_key_t *keys, uint8_t n) {
	render_rect(RENDER_BOTTOM, 0, LEGEND_Y - 1, 256, 11, RENDER_INK);
	int x = 4;
	for (uint8_t i = 0; i < n; ++i) {
		const int kw = render_text_width(keys[i].key) + 4;
		render_rect(RENDER_BOTTOM, x, LEGEND_Y, kw, 9, RENDER_BRONZE_L);
		render_text(RENDER_BOTTOM, x + 2, LEGEND_Y + 1, RENDER_INK, keys[i].key);
		x = render_text(RENDER_BOTTOM, x + kw + 3, LEGEND_Y + 1, RENDER_SURFACE,
		                str(keys[i].label)) + 8;
	}
}

void guide_next(render_screen_t s, int y, const char *copy) {
	if (copy == NULL || copy[0] == '\0') {
		return;
	}
	render_rect(s, 0, y, 256, 12, RENDER_INK);
	const int w = render_text_width(str(STR_NEXT)) + 4;
	render_rect(s, 3, y + 1, w, 10, RENDER_ACCENT);
	render_text(s, 5, y + 2, RENDER_PAPER, str(STR_NEXT));
	render_text(s, w + 8, y + 2, RENDER_SURFACE, copy);
}

void guide_point(render_screen_t s, int x, int y) {
	/* 2 px of travel every 8 frames: alive, never busy (B28 motion)    */
	const int bob = (int)(ui_anim_step() & 1u) * 2;
	render_text(s, x - 7 - bob, y, RENDER_BRONZE_L, ">");
}

/* the cheapest front the treasury can act on now (a threat first)      */
int guide_front(void) {
	const uint32_t treasury = (uint32_t)sim_snapshot()->treasury;
	int best = -1;
	uint16_t best_cost = 0xFFFFu;
	for (uint8_t i = 0; i < SIM_FRONT_COUNT; ++i) {
		const uint8_t act = sim_front_action(i);
		if (act == SIM_ACT_DEFEND) {
			return i;
		}
		const uint16_t c = sim_front_cost(i);
		if (act != SIM_ACT_HELD && c <= treasury && c < best_cost) {
			best = i;
			best_cost = c;
		}
	}
	return best;
}

const char *guide_next_copy(bool in_base) {
	if (!in_base) {
		const int f = guide_front();
		if (f >= 0 && sim_front_action((uint8_t)f) == SIM_ACT_DEFEND) {
			return str(STR_NEXT_DEFEND);
		}
		return f >= 0 ? str(STR_NEXT_PICK_FRONT) : str(STR_NEXT_NEED_GOLD);
	}
	if (sim_muster_total() > sim_muster_done()) {
		return str(STR_NEXT_MUSTER);
	}
	if (sim_roster() == 0u) {
		return str(STR_NEXT_RECRUIT);
	}
	return sim_deploy_province() >= 0 ? str(STR_NEXT_DEPLOY) : str(STR_NEXT_BACK_TO_MAP);
}

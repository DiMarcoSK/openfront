/* ui_draw.c - the copy channel on the art (module: ui).
 * Each screen hands its row table to ui_draw_layout() at the top of its
 * frame; ui_draw_text() places a row where the table says, word-wrapped
 * in the 5x7 face. Rows 18..23 belong to the L+R+START dev overlay and
 * land on an ink plate over the top screen while it is open.
 */
#include <nds.h>
#include <stdio.h>

#include "contracts/queue.h"   /* diag_overlay_page()                   */
#include "contracts/render.h"
#include "ui_draw.h"

static const ui_row_t *layout;
static uint8_t layout_n;
static uint32_t anim_frames;

void ui_draw_layout(const ui_row_t *rows, uint8_t n) {
	layout = rows;
	layout_n = rows ? n : 0u;
}

uint32_t ui_anim_step(void) {
	return anim_frames / 8u;
}

void ui_anim_tick(void) {
	anim_frames++;
}

void ui_draw_text(int x, int y, const char *txt) {
	(void)x;
	for (uint8_t i = 0; i < layout_n; ++i) {
		const ui_row_t *r = &layout[i];
		if (r->row == (uint8_t)y) {
			render_text_wrap((render_screen_t)r->screen, r->x, r->y,
			                 r->w, r->lines, r->color, txt);
			return;
		}
	}
	if (y >= 18 && y <= 23 && diag_overlay_page() != 0u) {
		const int py = 132 + (y - 18) * 10;
		if (y == 18 || (y == 20)) {
			render_rect(RENDER_TOP, 0, py - 2, 256, 192 - py + 2,
			            RENDER_INK);
		}
		render_text(RENDER_TOP, 4, py, RENDER_BRONZE_L, txt);
	}
}

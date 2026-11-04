/* ui_draw.h - internal helper shared by ui, catalog and components.
 * ui_draw_text(x, y, txt) is the screens' copy channel: `y` is a semantic
 * row (the host tests spy on it, byte-exact). On the DS each screen
 * registers a layout table that places its rows on the art - a row the
 * table does not list stays semantic only (console mirror, tests).       */
#ifndef WAR_TABLE_UI_DRAW_H
#define WAR_TABLE_UI_DRAW_H

#include <stdint.h>

typedef struct ui_row {
	uint8_t row;     /* semantic row 0..23                              */
	uint8_t screen;  /* render_screen_t                                 */
	int16_t x, y;    /* pixel origin of the first line                  */
	int16_t w;       /* wrap width in px                                */
	uint8_t lines;   /* max wrapped lines (9 px each)                   */
	uint8_t color;   /* RENDER_INK ...                                  */
} ui_row_t;

void ui_draw_text(int x, int y, const char *txt);
/* ui_draw_layout(): the row table of the frame being drawn (NULL = none). */
void ui_draw_layout(const ui_row_t *rows, uint8_t n);
/* ui_anim_step(): the shared animation clock (1 step = 8 frames).       */
uint32_t ui_anim_step(void);
void ui_anim_tick(void);
void tokens_load_main(void);
void tokens_load_sub(void);

#endif

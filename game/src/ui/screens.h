/* screens.h - module-private header shared by the ui screens (campaign,
 * base, battle, result). Each screen owns its input handling and drawing;
 * screen switching stays in ui.c ("loop owns switches", ui.h v1).
 */
#ifndef WAR_TABLE_UI_SCREENS_H
#define WAR_TABLE_UI_SCREENS_H

#include <stdint.h>
#include "contracts/ui.h" /* ui_screen_t (that contract owns the enum) */

/* One input event, already routed by ui.c (conduit -> ui_input).        */
void campaign_input(uint8_t kind, uint32_t value);
void campaign_frame(void);
void campaign_focus_province(uint8_t province);
/* campaign_focus_row(): the focused list row of the live campaign
 * screen; read by ui for the coalesced commit (B13 selection).           */
uint8_t campaign_focus_row(void);
/* campaign_request(): the screen ui.c should switch to this frame
 * (UI_SCREEN_NONE = none); the value resets on read.                     */
ui_screen_t campaign_request(void);

void base_input(uint8_t kind, uint32_t value);
void base_frame(void);
ui_screen_t base_request(void);

void battle_input(uint8_t kind, uint32_t value);
void battle_frame(void);

void result_input(uint8_t kind, uint32_t value);
void result_frame(void);
/* result_request(): Campaign once the Continue commit settled (or A).  */
ui_screen_t result_request(void);

/* battle_holds(): the order panel or the boss intro is up (B22 hold). */
bool battle_holds(void);
/* battle_enter(): a fresh deploy opens on the intro (god fronts) or the
 * field; Retry re-enters without the intro.                            */
void battle_enter(bool intro);

/* Settings (B24): the owner of every save action; `from` = host screen. */
void settings_open(ui_screen_t from);
void settings_input(uint8_t kind, uint32_t value);
void settings_frame(void);
ui_screen_t settings_request(void);

/* ---- scene.c: the art dressing shared by the screens ------------------- */
#include <stddef.h>
#include "contracts/render.h"

typedef struct front_art {
	uint8_t map;      /* RENDER_BD_MAP_* (top field)                     */
	uint8_t panel;    /* RENDER_BD_BATTLE_* (bottom command frame)       */
	uint8_t enemy;    /* RENDER_CIV_*                                    */
	uint8_t god;      /* RENDER_GOD_* or RENDER_GOD_NONE                 */
} front_art_t;

const front_art_t *front_art(uint8_t province);
const char *god_name(uint8_t god);

enum { SAVE_ROW_TITLE = 0, SAVE_ROW_RESULT, SAVE_ROW_SETTINGS };
/* save_row(): the 2-line SaveStatus row of a host, spec-06 6.6.        */
void save_row(uint8_t host, char *l1, char *l2, size_t n);
void save_row_paint(render_screen_t scr, uint8_t host, int x, int y, int w);

#endif /* WAR_TABLE_UI_SCREENS_H */

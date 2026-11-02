/* ui.h v1 - ui module contract (contributes no state, consumes snapshots).
 * Owner: ui module (src/ui). It renders the 7 kept screens and emits
 * commands only; it never writes run state or EEPROM (constitution 6).
 * Breaking change = ui_v2.h + adapter (constitution 6).
 */
#ifndef WAR_TABLE_UI_H_V1
#define WAR_TABLE_UI_H_V1

#include <stdint.h>

#define UI_ABI_VERSION 1u

typedef enum ui_screen {
	UI_SCREEN_TITLE = 0,
	UI_SCREEN_CAMPAIGN,
	UI_SCREEN_BASE,
	UI_SCREEN_BATTLE,
	UI_SCREEN_RESULT,
	UI_SCREEN_SETTINGS,
	UI_SCREEN_NONE,
} ui_screen_t;

/* ui_screen(): current screen (spec 01-06 map it to the state machine).  */
ui_screen_t ui_screen(void);

/* ui_frame(): draw the current screen once; called by loop each VBlank.  */
void ui_frame(void);

/* ui_set_screen(): switch the top-level screen (loop owns switches).     */
void ui_set_screen(ui_screen_t screen);

#endif /* WAR_TABLE_UI_H_V1 */

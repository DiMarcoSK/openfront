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

#endif /* WAR_TABLE_UI_SCREENS_H */

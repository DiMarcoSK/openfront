/* ui_draw.c - one terminal helper used by the /ui catalog and the stubs.
 * The tilemap renderer ships with B07/B30; until then, text-only is the
 * honest runner for state copy.
 */
#include <nds.h>
#include <stdio.h>

void ui_draw_text(int x, int y, const char *txt) {
	char buf[64];
	snprintf(buf, sizeof buf, "\x1b[%d;%dH%s", y + 1, 1, txt);
	iprintf("%s", buf);
}

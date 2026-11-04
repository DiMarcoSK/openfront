/* guide.h - control legend, NEXT plate and pointer (module-private to
 * ui, B27 onboarding). See guide.c.                                      */
#ifndef WAR_TABLE_UI_GUIDE_H
#define WAR_TABLE_UI_GUIDE_H

#include <stdbool.h>
#include <stdint.h>

#include "contracts/render.h"
#include "strings.h"

typedef struct guide_key {
	const char *key;   /* the key cap: "A", "B", "START", "+"            */
	str_id_t label;    /* the word for what it does here                 */
} guide_key_t;

/* the strip at the foot of the bottom screen (y 181..191)              */
void guide_legend(const guide_key_t *keys, uint8_t n);
/* the NEXT plate: 12 px tall at y on screen s                          */
void guide_next(render_screen_t s, int y, const char *copy);
/* a bobbing chevron left of x, on text row y                           */
void guide_point(render_screen_t s, int x, int y);
/* the front the next step points at (a threat first, else the cheapest
 * affordable one), -1 when the treasury can act on none                */
int guide_front(void);
/* the one step that moves the campaign on, from the live state         */
const char *guide_next_copy(bool in_base);

#endif /* WAR_TABLE_UI_GUIDE_H */

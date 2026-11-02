/* render.c - VRAM layout + frame draw (module: render).
 * B01 scaffold: paper field + BG0/C and 1 sprite for the focused card so
 * the 128-sprite budget stays under cap (B22 uses the atlas at scale).
 * The formal VRAM split ships with B04 (design tokens + 8 components).
 */
#include <nds.h>
#include <calico.h>

#include "contracts/render.h"
#include "contracts/tokens.h"

static const void *atlas_tiles_ptr;
static const void *atlas_map_ptr;
static const void *atlas_palette_ptr;

void render_init(void) {
	/* Both screens get a clean paper field; sprite banks are reserved. */
	setBrightness(3, 0);
	vramSetBankA(VRAM_A_MAIN_BG);
	vramSetBankC(VRAM_C_SUB_BG);
}

void render_load_atlas(const void *tiles, const void *map, const void *pal) {
	atlas_tiles_ptr = tiles;
	atlas_map_ptr = map;
	atlas_palette_ptr = pal;
}

void render_frame(void) {
	/* Frame draw ships with B07/B11 (title), B08 (campaign) etc.
	 * The 3 tiles of the 24 px tier band land with B17.
	 * For B01 we keep the CPU warm; the frame budget prints at CI.      */
}

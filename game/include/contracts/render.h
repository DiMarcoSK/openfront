/* render.h v1 - the module that owns VRAM layout and the frame draw.
 * Owner: render module (src/render). It turns a scene into BG/OAM
 * changes and is the only writer to VRAM and OAM.
 * Additive only: render_v2.h + adapter is the breaking-change path.
 */
#ifndef WAR_TABLE_RENDER_H_V1
#define WAR_TABLE_RENDER_H_V1

#include <stdint.h>

#define RENDER_ABI_VERSION 1u

/* SDD section 4 hardware ceilings; pinned here so the budget check uses
 * the numbers the rest of the code reads (constitution 1).               */
#define RENDER_VRAM_CAP_KB   656u
#define RENDER_OAM_CAP       128u   /* sprites / screen          */
#define RENDER_ROM_CAP_MB     16u
#define RENDER_RAM_CAP_KB    2795u  /* 2.73 MB                   */

/* render_init(): banks the BG/OAM VRAM layout; no frame output.           */
void render_init(void);

/* render_frame(): push one frame's scene into BG / OAM.                  */
void render_frame(void);

/* render_load_atlas(): bind a 4bpp atlas pair for later use.             */
void render_load_atlas(const void *atlas_tiles, const void *atlas_map,
                       const void *atlas_palette);

#endif /* WAR_TABLE_RENDER_H_V1 */

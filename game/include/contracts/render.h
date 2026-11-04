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

/* ---- additive: the art pass (B22 battle view, B30 brand) ----------------
 * Each screen has three layers: the 8bpp art backdrop (BG3, priority 3),
 * the sprites (priority 2, or 0 with RENDER_SPR_FRONT) and a 4bpp overlay
 * (BG0, priority 1) that carries every live value - text, panels,
 * buttons, bars and the 2 px focus ring. The overlay is immediate mode:
 * ui re-issues its draw calls every frame; render rasterizes only when
 * the call list changed and uploads it in the next VBlank.
 * Frame order (loop): render_vblank() -> ... -> render_begin() ->
 * ui_frame() -> render_frame().                                          */

typedef enum render_screen {
	RENDER_TOP = 0,
	RENDER_BOTTOM = 1,
} render_screen_t;

/* Backdrops. The MAP_* ones are 512x512 fields scrolled by the camera
 * (top screen); every other one is a 256x192 still.                      */
typedef enum render_backdrop {
	RENDER_BD_NONE = 0,
	RENDER_BD_TITLE_TOP,
	RENDER_BD_FRAME_TITLE,
	RENDER_BD_FRAME_SHEET,
	RENDER_BD_RESULT_WIN,
	RENDER_BD_RESULT_LOSS,
	RENDER_BD_BATTLE_PLAINS,   /* bottom command panel per field        */
	RENDER_BD_BATTLE_DESERT,
	RENDER_BD_BATTLE_HIGHLANDS,
	RENDER_BD_BATTLE_ARENA,
	RENDER_BD_MAP_PLAINS,      /* top 512x512 fields                    */
	RENDER_BD_MAP_DESERT,
	RENDER_BD_MAP_HIGHLANDS,
	RENDER_BD_MAP_ARENA,       /* recoloured per god (the god argument) */
	RENDER_BD_STAGE,           /* boss stage of `god`, palette cycled   */
	RENDER_BD_CARD,            /* boss intro card of `god`              */
	RENDER_BD_COUNT,
} render_backdrop_t;

/* Overlay colours: the UI kit slots (palette bank 15 = the pins sheet). */
enum {
	RENDER_CLEAR = 0, RENDER_INK, RENDER_INK2, RENDER_LINE, RENDER_PAPER,
	RENDER_SURFACE, RENDER_ACCENT, RENDER_ACCENT_L, RENDER_WARN,
	RENDER_WARN_L, RENDER_CRIT, RENDER_CRIT_L, RENDER_BRONZE_D,
	RENDER_BRONZE, RENDER_BRONZE_L, RENDER_IRON,
};

typedef enum render_panel_kind {
	RENDER_PANEL_SHEET = 0, RENDER_PANEL_IRON, RENDER_PANEL_BOSS,
} render_panel_kind_t;

typedef enum render_button_state {
	RENDER_BTN_NORMAL = 0, RENDER_BTN_FOCUS, RENDER_BTN_PRESSED,
	RENDER_BTN_DISABLED,
} render_button_state_t;

typedef enum render_bar_kind {
	RENDER_BAR_HP = 0, RENDER_BAR_MORALE, RENDER_BAR_BOSS,
} render_bar_kind_t;

/* Sprite vocabulary (semantic ids; render owns the sheet mapping).       */
enum { RENDER_CIV_HELLENES = 0, RENDER_CIV_ROME, RENDER_CIV_CARTHAGE,
       RENDER_CIV_ASSYRIA, RENDER_CIVS };
enum { RENDER_CLS_SPEAR = 0, RENDER_CLS_BLADE, RENDER_CLS_ARCHER,
       RENDER_CLS_CAVALRY, RENDER_CLS_GENERAL, RENDER_CLASSES };
enum { RENDER_GOD_ZEUS = 0, RENDER_GOD_POSEIDON, RENDER_GOD_HADES,
       RENDER_GOD_ARES, RENDER_GOD_KRONOS, RENDER_GODS,
       RENDER_GOD_NONE = 0xFF };
/* squad animation / portrait animation                                   */
enum { RENDER_ANIM_IDLE = 0, RENDER_ANIM_WALK, RENDER_ANIM_ATTACK,
       RENDER_ANIM_COMMAND, RENDER_ANIM_HURT, RENDER_ANIM_DEATH };
enum { RENDER_GOD_IDLE = 0, RENDER_GOD_WALK, RENDER_GOD_ATTACK,
       RENDER_GOD_SPECIAL, RENDER_GOD_HURT, RENDER_GOD_DEATH };
enum { RENDER_ICON_GOLD = 0, RENDER_ICON_FOOD, RENDER_ICON_MATERIALS,
       RENDER_ICON_KNOWLEDGE, RENDER_ICON_MANPOWER, RENDER_ICON_MORALE,
       RENDER_ICON_ATTACK, RENDER_ICON_DEFENSE, RENDER_ICON_SPEED1,
       RENDER_ICON_SPEED2, RENDER_ICON_SPEED4, RENDER_ICON_PAUSE,
       RENDER_ICON_CASUALTIES, RENDER_ICON_TIER, RENDER_ICON_BATTLE };

#define RENDER_SPR_FLIP  0x1u  /* h-flip (enemies face left)            */
#define RENDER_SPR_FRONT 0x2u  /* draw over the overlay (priority 0)    */

/* frame cycle */
void render_vblank(void);
void render_begin(void);

/* backdrop of one screen; god picks the arena palette / stage / card.
 * Loads only when (backdrop, god) changed.                               */
void render_backdrop(render_screen_t s, uint8_t backdrop, uint8_t god);
void render_camera(int x, int y);   /* scroll of a top MAP_* backdrop   */
void render_flash(uint8_t level);   /* top brightness 0..16 (bolt flash) */
/* the ground of a MAP_* field (additive, art_terrain.h): the y offset a
 * lane walks at map x round rock and water, and the x span it can walk */
int  render_lane_dy(uint8_t backdrop, uint8_t lane, int x);
void render_lane_span(uint8_t backdrop, uint8_t lane, int *x0, int *x1);

/* overlay drawing (pixel coordinates, 256x192 per screen)                */
int  render_text(render_screen_t s, int x, int y, uint8_t color,
                 const char *text);              /* returns the end x    */
int  render_text_wrap(render_screen_t s, int x, int y, int w, int lines,
                      uint8_t color, const char *text); /* lines used    */
int  render_text_big(render_screen_t s, int x, int y, uint8_t color,
                     const char *text);          /* 2x numerals/titles   */
int  render_text_width(const char *text);        /* 6 px advance         */
void render_rect(render_screen_t s, int x, int y, int w, int h,
                 uint8_t color);
void render_box(render_screen_t s, int x, int y, int w, int h,
                uint8_t color);
void render_ring(render_screen_t s, int x, int y, int w, int h);
void render_panel(render_screen_t s, int x, int y, int w, int h,
                  uint8_t kind);
void render_button(render_screen_t s, int x, int y, int w, int h,
                   const char *label, uint8_t state);
void render_bar(render_screen_t s, int x, int y, int w, uint32_t num,
                uint32_t den, uint8_t kind);

/* sprites (screen coordinates; off-screen ones are culled)               */
void render_squad(uint8_t civ, uint8_t cls, uint8_t strength, uint8_t anim,
                  uint32_t step, int x, int y, uint8_t flags);
void render_portrait(render_screen_t s, uint8_t civ, uint8_t cls,
                     uint8_t anim, uint32_t step, int x, int y,
                     uint8_t flags);
void render_icon(render_screen_t s, uint8_t icon, int x, int y,
                 uint8_t flags);
void render_pin(render_screen_t s, uint8_t threat, int x, int y,
                uint8_t flags);
void render_relic(render_screen_t s, uint8_t god, int x, int y,
                  uint8_t flags);
/* god = 128x128 (4 OAM), anchored top-left; at most one per frame.      */
void render_god(uint8_t god, uint8_t anim, uint32_t step, int x, int y,
                uint8_t flags);
uint8_t render_god_frames(uint8_t anim);  /* frames in that animation   */
void render_fx(uint8_t god, uint8_t frame, int x, int y, uint8_t flags);

/* render_overlay_hash(): FNV-1a of the last presented overlay call list
 * of a screen - the golden frame hash now covers the art layer (B31).   */
uint32_t render_overlay_hash(render_screen_t s);

#endif /* WAR_TABLE_RENDER_H_V1 */

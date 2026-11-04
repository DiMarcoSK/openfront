/* render.c - VRAM layout + frame draw (module: render).
 * B01 scaffold -> art pass: the only writer to VRAM, OAM and palette RAM.
 * Per screen (bank A main BG, B main OBJ, C sub BG, D sub OBJ):
 *   BG3  8bpp art backdrop, priority 3: map 8 KB @ 8 KB, tiles @ 48 KB
 *        (512x512 battle fields up to 63 KB, 256x192 stills up to 45 KB);
 *   BG0  4bpp overlay, priority 1: map 2 KB @ 0, 768 unique tiles
 *        (24 KB) @ 16 KB, palette bank 15 = the UI kit (pins sheet);
 *   OBJ  1D/128 mapping, one 512 B block per OAM slot (0..119), the god's
 *        four 2 KB blocks at 64 KB and its effect at 72 KB.
 * The overlay is immediate mode: ui re-issues its calls every frame into
 * an op list; the list is hashed and rasterized only when it changed,
 * then uploaded in the next VBlank (no tearing, ~0 cost on still frames).
 */
#include <nds.h>
#include <calico.h>
#include <string.h>

#include "contracts/render.h"

#include "art_assets_game.h" /* the table minus the design mock-ups */
#include "art_index.h"
#include "art_palettes.h"

#define SCREENS 2
#define OV_TILES 768u                 /* 32 x 24 tiles                  */
#define OV_WORDS (OV_TILES * 8u)      /* 8 rows x 8 nibbles per tile    */
#define OV_BANK 15u
#define ART_PAL_ENTRIES 240u          /* bank 15 belongs to the overlay */

#define OBJ_SLOTS 120
#define OBJ_BLOCK 512u
#define GOD_SLOT 120
#define FX_SLOT 124
#define GOD_GFX_OFF (64u * 1024u)
#define FX_GFX_OFF (72u * 1024u)

/* OBJ palette banks (16 colours each)                                    */
enum { PAL_CIV0 = 0, PAL_ICONS = 4, PAL_RELICS = 5, PAL_PINS = 6, PAL_GOD = 7 };

/* ---- art lookups (alphabetical grit enums -> semantic ids) ------------ */
static const uint8_t CIV_ALPHA[RENDER_CIVS] = {2, 3, 1, 0};
static const uint8_t CLS_ALPHA[RENDER_CLASSES] = {4, 1, 0, 2, 3};
static const uint8_t GOD_ALPHA[RENDER_GODS] = {4, 3, 1, 0, 2};

static int squad_obj(uint8_t civ, uint8_t cls) {
	return ART_OBJ_SQUAD_ASSYRIA_ARCHER + CIV_ALPHA[civ % RENDER_CIVS] * 5 +
	       CLS_ALPHA[cls % RENDER_CLASSES];
}

static int unit_obj(uint8_t civ, uint8_t cls) {
	return ART_OBJ_UNIT_ASSYRIA_ARCHER + CIV_ALPHA[civ % RENDER_CIVS] * 5 +
	       CLS_ALPHA[cls % RENDER_CLASSES];
}

static const uint16_t *const ARENA_PAL[RENDER_GODS] = {
	ART_ARENA_PAL_ZEUS, ART_ARENA_PAL_POSEIDON, ART_ARENA_PAL_HADES,
	ART_ARENA_PAL_ARES, ART_ARENA_PAL_KRONOS,
};

/* boss stage palette cycles: {first, count, period in frames} x 3       */
static const uint8_t STAGE_CYC[RENDER_GODS][3][3] = {
	{{ART_CYCLE_BOSS_ZEUS_EYES, 6}},
	{{ART_CYCLE_BOSS_POSEIDON_EYES, 10}, {ART_CYCLE_BOSS_POSEIDON_SURF, 14}},
	{{ART_CYCLE_BOSS_HADES_SOULFIRE, 9}, {ART_CYCLE_BOSS_HADES_EMBERS, 5}},
	{{ART_CYCLE_BOSS_ARES_FIRE, 4}},
	{{ART_CYCLE_BOSS_KRONOS_SAND, 5}, {ART_CYCLE_BOSS_KRONOS_EYES, 12},
	 {ART_CYCLE_BOSS_KRONOS_TICKS, 20}},
};

/* ---- the 5x7 face, decoded once from the font sheet ------------------- */
static uint8_t glyph_rows[96][7];

static void font_decode(void) {
	const uint8_t *g = (const uint8_t *)ART_OBJ[ART_OBJ_FONT].gfx;
	for (int c = 0; c < 96; ++c) {
		for (int y = 0; y < 7; ++y) {
			uint8_t bits = 0u;
			for (int x = 0; x < 5; ++x) {
				const int px = x + 1; /* glyphs sit at x + 1  */
				const uint8_t b = g[c * 32 + y * 4 + px / 2];
				const uint8_t n = (px & 1) ? (b >> 4) : (b & 0xFu);
				if (n != 0u) {
					bits |= (uint8_t)(1u << x);
				}
			}
			glyph_rows[c][y] = bits;
		}
	}
}

/* ---- overlay op list --------------------------------------------------- */
enum { OP_TEXT = 1, OP_WRAP, OP_BIG, OP_RECT, OP_BOX, OP_RING, OP_PANEL,
       OP_BUTTON, OP_BAR };

typedef struct {
	uint8_t type, color, kind, lines;
	int16_t x, y, w, h;
	uint16_t str;
	uint32_t num, den;
} op_t;

#define MAX_OPS 320
#define POOL 4096

typedef struct {
	op_t ops[MAX_OPS];
	uint16_t n_ops;
	char pool[POOL];
	uint16_t pool_used;
	uint32_t hash_last;
	bool upload;
	uint32_t canvas[OV_WORDS];
} overlay_t;

static overlay_t ov[SCREENS];

static uint16_t pool_add(overlay_t *o, const char *s) {
	const size_t n = strlen(s) + 1u;
	if (o->pool_used + n > POOL) {
		return 0xFFFFu;
	}
	const uint16_t at = o->pool_used;
	memcpy(o->pool + at, s, n);
	o->pool_used = (uint16_t)(o->pool_used + n);
	return at;
}

static op_t *op_new(render_screen_t s, uint8_t type) {
	overlay_t *o = &ov[s & 1];
	if (o->n_ops >= MAX_OPS) {
		return NULL;
	}
	op_t *p = &o->ops[o->n_ops++];
	memset(p, 0, sizeof *p);
	p->type = type;
	p->str = 0xFFFFu;
	return p;
}

/* ---- rasterizer -------------------------------------------------------- */
static uint32_t *cv_cur;

static inline void px(int x, int y, uint8_t c) {
	if ((unsigned)x >= 256u || (unsigned)y >= 192u) {
		return;
	}
	uint32_t *w = &cv_cur[((y >> 3) * 32 + (x >> 3)) * 8 + (y & 7)];
	const uint32_t sh = (uint32_t)(x & 7) * 4u;
	*w = (*w & ~(0xFu << sh)) | ((uint32_t)c << sh);
}

static void hline(int x0, int x1, int y, uint8_t c) {
	if ((unsigned)y >= 192u) {
		return;
	}
	if (x0 < 0) x0 = 0;
	if (x1 > 255) x1 = 255;
	const uint32_t fill = (uint32_t)c * 0x11111111u;
	int x = x0;
	while (x <= x1 && (x & 7) != 0) {
		px(x++, y, c);
	}
	while (x + 7 <= x1) {
		cv_cur[((y >> 3) * 32 + (x >> 3)) * 8 + (y & 7)] = fill;
		x += 8;
	}
	while (x <= x1) {
		px(x++, y, c);
	}
}

static void fill(int x, int y, int w, int h, uint8_t c) {
	for (int yy = y; yy < y + h; ++yy) {
		hline(x, x + w - 1, yy, c);
	}
}

static int glyph_index(char ch) {
	const unsigned char u = (unsigned char)ch;
	return (u >= 32u && u < 127u) ? (int)(u - 32u) : ('?' - 32);
}

static int draw_text(int x, int y, uint8_t c, const char *s, int scale) {
	for (; *s; ++s) {
		const uint8_t *g = glyph_rows[glyph_index(*s)];
		for (int gy = 0; gy < 7; ++gy) {
			for (int gx = 0; gx < 5; ++gx) {
				if (g[gy] & (1u << gx)) {
					if (scale == 1) {
						px(x + gx, y + gy, c);
					} else {
						fill(x + gx * 2, y + gy * 2, 2, 2, c);
					}
				}
			}
		}
		x += 6 * scale;
	}
	return x;
}

/* word wrap: emits up to `lines` rows of at most w/6 chars. `out` NULL =
 * count only (the record pass uses it to answer the line count).        */
static int wrap(int x, int y, int w, int lines, uint8_t c, const char *s,
                bool paint) {
	const int cols = (w / 6) > 0 ? (w / 6) : 1;
	char buf[64];
	int used = 0;
	while (*s && used < lines) {
		while (*s == ' ') {
			++s;
		}
		int len = (int)strlen(s);
		int take = len;
		if (len > cols) {
			take = cols;
			while (take > 0 && s[take] != ' ') {
				--take;
			}
			if (take == 0) {
				take = cols; /* one long word: hard break */
			}
		}
		if (take > (int)sizeof buf - 1) {
			take = (int)sizeof buf - 1;
		}
		if (paint) {
			memcpy(buf, s, (size_t)take);
			buf[take] = '\0';
			draw_text(x, y + used * 9, c, buf, 1);
		}
		s += take;
		++used;
	}
	return used;
}

static void corner(int cx, int cy, int sx, int sy) {
	for (int i = 0; i < 7; ++i) {
		px(cx + sx * i, cy, RENDER_BRONZE_D);
		px(cx, cy + sy * i, RENDER_BRONZE_D);
	}
	for (int i = 1; i < 6; ++i) {
		px(cx + sx * i, cy + sy, RENDER_BRONZE);
		px(cx + sx, cy + sy * i, RENDER_BRONZE);
	}
	px(cx + sx, cy + sy, RENDER_BRONZE_L);
	px(cx + sx * 3, cy + sy * 3, RENDER_BRONZE_D);
	px(cx + sx * 3 - sx, cy + sy * 3 - sy, RENDER_BRONZE_L);
}

/* the ui.py 9-slice panel, pixel for pixel                              */
static void draw_panel(int x, int y, int w, int h, uint8_t kind) {
	static const uint8_t FILL[3] = {RENDER_PAPER, RENDER_IRON, RENDER_CRIT};
	static const uint8_t INNER[3] = {RENDER_SURFACE, RENDER_INK, RENDER_IRON};
	kind = (kind > 2u) ? 0u : kind;
	fill(x, y, w, h, RENDER_INK);
	fill(x + 1, y + 1, w - 2, h - 2, FILL[kind]);
	fill(x + 3, y + 3, w - 6, h - 6, INNER[kind]);
	if (kind == RENDER_PANEL_SHEET) {
		for (int i = 4; i < w - 4; i += 4) {
			px(x + i, y + 2, RENDER_LINE);
			px(x + i, y + h - 3, RENDER_LINE);
		}
		for (int i = 4; i < h - 4; i += 4) {
			px(x + 2, y + i, RENDER_LINE);
			px(x + w - 3, y + i, RENDER_LINE);
		}
	} else {
		hline(x + 1, x + w - 2, y + 1,
		      kind == RENDER_PANEL_IRON ? RENDER_BRONZE_D : RENDER_CRIT_L);
		hline(x + 2, x + w - 3, y + h - 3, RENDER_INK);
	}
	corner(x, y, 1, 1);
	corner(x + w - 1, y, -1, 1);
	corner(x, y + h - 1, 1, -1);
	corner(x + w - 1, y + h - 1, -1, -1);
}

static void draw_button(int x, int y, int w, int h, const char *label,
                        uint8_t st) {
	uint8_t ink;
	if (st == RENDER_BTN_DISABLED) {
		fill(x, y, w, h, RENDER_PAPER);
		for (int i = 0; i < w; i += 2) {
			px(x + i, y, RENDER_INK2);
			px(x + i, y + h - 1, RENDER_INK2);
		}
		for (int i = 0; i < h; i += 2) {
			px(x, y + i, RENDER_INK2);
			px(x + w - 1, y + i, RENDER_INK2);
		}
		ink = RENDER_INK2;
	} else {
		const uint8_t base = st == RENDER_BTN_FOCUS     ? RENDER_ACCENT
		                     : st == RENDER_BTN_PRESSED ? RENDER_INK
		                                                : RENDER_SURFACE;
		fill(x, y, w, h, RENDER_INK);
		fill(x + 1, y + 1, w - 2,
		     st == RENDER_BTN_PRESSED ? h - 2 : h - 3, base);
		if (st == RENDER_BTN_FOCUS) {
			hline(x + 1, x + w - 2, y + 1, RENDER_ACCENT_L);
			hline(x + 1, x + w - 2, y + h - 2, RENDER_INK);
			px(x + 1, y + 1, RENDER_BRONZE_L);
			px(x + w - 2, y + 1, RENDER_BRONZE_L);
			for (int k = 0; k < 3; ++k) {
				px(x + 3 + k, y + h / 2 - 2 + k, RENDER_BRONZE_L);
				px(x + 3 + k, y + h / 2 + 2 - k, RENDER_BRONZE_L);
			}
		} else if (st == RENDER_BTN_NORMAL) {
			hline(x + 1, x + w - 2, y + h - 2, RENDER_LINE);
		}
		ink = st == RENDER_BTN_FOCUS     ? RENDER_SURFACE
		      : st == RENDER_BTN_PRESSED ? RENDER_BRONZE_L
		                                 : RENDER_INK;
	}
	const int tw = render_text_width(label);
	const int tx = x + (w - tw) / 2;
	const int ty = y + (h - 7) / 2 - (st == RENDER_BTN_PRESSED ? 0 : 1) +
	               (st == RENDER_BTN_PRESSED ? 1 : 0);
	if (st == RENDER_BTN_FOCUS) {
		draw_text(tx + 1, ty + 1, RENDER_INK, label, 1);
	}
	draw_text(tx, ty, ink, label, 1);
}

static void draw_bar(int x, int y, int w, uint32_t num, uint32_t den,
                     uint8_t kind) {
	static const uint8_t COL[3] = {RENDER_ACCENT, RENDER_WARN, RENDER_CRIT};
	static const uint8_t HI[3] = {RENDER_ACCENT_L, RENDER_WARN_L, RENDER_CRIT_L};
	kind = (kind > 2u) ? 0u : kind;
	fill(x, y + 1, w, 6, RENDER_INK);
	fill(x + 2, y + 2, w - 4, 4, RENDER_IRON);
	const int fw = den ? (int)((uint32_t)(w - 4) * (num > den ? den : num) / den)
	                   : 0;
	if (fw > 0) {
		fill(x + 2, y + 2, fw, 4, COL[kind]);
		hline(x + 2, x + 1 + fw, y + 2, HI[kind]);
	}
	for (int k = 1; k <= 3; ++k) {
		px(x + 2 + (w - 4) * k / 4, y + 5, RENDER_INK);
	}
	for (int i = 0; i < 8; ++i) {
		px(x, y + i, RENDER_BRONZE);
		px(x + w - 1, y + i, RENDER_BRONZE);
	}
	px(x, y, RENDER_BRONZE_L);
	px(x + w - 1, y, RENDER_BRONZE_L);
}

static void raster(overlay_t *o) {
	cv_cur = o->canvas;
	memset(o->canvas, 0, sizeof o->canvas);
	for (uint16_t i = 0; i < o->n_ops; ++i) {
		const op_t *p = &o->ops[i];
		const char *s = (p->str != 0xFFFFu) ? o->pool + p->str : "";
		switch (p->type) {
		case OP_TEXT: draw_text(p->x, p->y, p->color, s, 1); break;
		case OP_BIG: draw_text(p->x, p->y, p->color, s, 2); break;
		case OP_WRAP:
			wrap(p->x, p->y, p->w, p->lines, p->color, s, true);
			break;
		case OP_RECT: fill(p->x, p->y, p->w, p->h, p->color); break;
		case OP_BOX:
			hline(p->x, p->x + p->w - 1, p->y, p->color);
			hline(p->x, p->x + p->w - 1, p->y + p->h - 1, p->color);
			fill(p->x, p->y, 1, p->h, p->color);
			fill(p->x + p->w - 1, p->y, 1, p->h, p->color);
			break;
		case OP_RING:
			for (int k = 0; k < 2; ++k) {
				const uint8_t c = k ? RENDER_INK : RENDER_BRONZE_L;
				const int x = p->x - 2 + k, y = p->y - 2 + k;
				const int w = p->w + 4 - 2 * k, h = p->h + 4 - 2 * k;
				hline(x, x + w - 1, y, c);
				hline(x, x + w - 1, y + h - 1, c);
				fill(x, y, 1, h, c);
				fill(x + w - 1, y, 1, h, c);
			}
			break;
		case OP_PANEL: draw_panel(p->x, p->y, p->w, p->h, p->kind); break;
		case OP_BUTTON:
			draw_button(p->x, p->y, p->w, p->h, s, p->kind);
			break;
		case OP_BAR:
			draw_bar(p->x, p->y, p->w, p->num, p->den, p->kind);
			break;
		default: break;
		}
	}
}

static uint32_t fnv(uint32_t h, const void *data, size_t n) {
	const uint8_t *b = (const uint8_t *)data;
	for (size_t i = 0; i < n; ++i) {
		h ^= b[i];
		h *= 0x01000193u;
	}
	return h;
}

static uint32_t op_hash(const overlay_t *o) {
	uint32_t h = 0x811C9DC5u;
	h = fnv(h, o->ops, (size_t)o->n_ops * sizeof(op_t));
	return fnv(h, o->pool, o->pool_used);
}

/* ---- backdrops ---------------------------------------------------------- */
typedef struct {
	uint8_t id, god;
	bool map;
} backdrop_state_t;

static backdrop_state_t bd[SCREENS];
static int bg_art[SCREENS], bg_ov[SCREENS];
static u16 pal[SCREENS][256];
static bool pal_dirty[SCREENS];
static int cam_x, cam_y;
static uint32_t frames;

static int art_for(uint8_t id, uint8_t god, bool *map, const u16 **over) {
	const uint8_t g = (god < RENDER_GODS) ? god : RENDER_GOD_ZEUS;
	*map = false;
	*over = NULL;
	switch (id) {
	case RENDER_BD_TITLE_TOP: return ART_BG_TITLE_TOP;
	case RENDER_BD_FRAME_TITLE: return ART_BG_FRAME_TITLE;
	case RENDER_BD_FRAME_SHEET: return ART_BG_FRAME_SHEET;
	case RENDER_BD_RESULT_WIN: return ART_BG_FRAME_RESULT_WIN;
	case RENDER_BD_RESULT_LOSS: return ART_BG_FRAME_RESULT_LOSS;
	case RENDER_BD_BATTLE_PLAINS: return ART_BG_FRAME_BATTLE_PLAINS;
	case RENDER_BD_BATTLE_DESERT: return ART_BG_FRAME_BATTLE_DESERT;
	case RENDER_BD_BATTLE_HIGHLANDS: return ART_BG_FRAME_BATTLE_HIGHLANDS;
	case RENDER_BD_BATTLE_ARENA: /* the minimap wears the god's arena */
		return ART_BG_FRAME_BATTLE_ARENA_ARES + GOD_ALPHA[g];
	case RENDER_BD_MAP_PLAINS: *map = true; return ART_BG_MAP_PLAINS;
	case RENDER_BD_MAP_DESERT: *map = true; return ART_BG_MAP_DESERT;
	case RENDER_BD_MAP_HIGHLANDS: *map = true; return ART_BG_MAP_HIGHLANDS;
	case RENDER_BD_MAP_ARENA:
		*map = true;
		*over = ARENA_PAL[g];
		return ART_BG_MAP_ARENA;
	case RENDER_BD_STAGE: return ART_BG_BOSS_ARES + GOD_ALPHA[g];
	case RENDER_BD_CARD: return ART_BG_CARD_ARES + GOD_ALPHA[g];
	default: return -1;
	}
}

void render_backdrop(render_screen_t s, uint8_t id, uint8_t god) {
	s = (render_screen_t)(s & 1);
	if (bd[s].id == id && bd[s].god == god) {
		return;
	}
	bd[s].id = id;
	bd[s].god = god;
	bool map;
	const u16 *over;
	const int a = art_for(id, god, &map, &over);
	bd[s].map = map;
	if (a < 0) {
		bgHide(bg_art[s]);
		return;
	}
	const art_bg_t *art = &ART_BG[a];
	const int layer = 3;
	bg_art[s] = (s == RENDER_TOP)
	    ? bgInit(layer, BgType_Text8bpp,
	             map ? BgSize_T_512x512 : BgSize_T_256x256, 4, 3)
	    : bgInitSub(layer, BgType_Text8bpp,
	                map ? BgSize_T_512x512 : BgSize_T_256x256, 4, 3);
	bgSetPriority(bg_art[s], 3);
	DC_FlushRange(art->tiles, art->tiles_len);
	DC_FlushRange(art->map, art->map_len);
	u16 *mp = bgGetMapPtr(bg_art[s]);
	if (!map) {
		memset(mp, 0, 2048); /* rows 24..31 of a still stay blank    */
	}
	dmaCopy(art->tiles, bgGetGfxPtr(bg_art[s]), art->tiles_len);
	dmaCopy(art->map, mp, art->map_len);
	const u16 *src = over ? over : (const u16 *)art->pal;
	uint32_t n = over ? 256u : art->pal_len / 2u;
	if (n > ART_PAL_ENTRIES) {
		n = ART_PAL_ENTRIES;
	}
	memset(pal[s], 0, ART_PAL_ENTRIES * 2u);
	memcpy(pal[s], src, n * 2u);
	pal_dirty[s] = true;
	bgSetScroll(bg_art[s], 0, 0);
	bgShow(bg_art[s]);
}

void render_camera(int x, int y) {
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (x > 256) x = 256;
	if (y > 320) y = 320;
	cam_x = x;
	cam_y = y;
}

void render_flash(uint8_t level) {
	setBrightness(1, level > 16u ? 16 : level);
}

static void rotate(u16 *p, int first, int count) {
	const u16 last = p[first + count - 1];
	for (int i = first + count - 1; i > first; --i) {
		p[i] = p[i - 1];
	}
	p[first] = last;
}

/* ---- sprites ------------------------------------------------------------- */
typedef struct {
	OamState *oam;
	u16 *gfx_base;
	int count, prev_count;
	const void *cache[128];
	bool god_drawn, fx_drawn;
} obj_state_t;

static obj_state_t obj[SCREENS];
static uint8_t god_pal_loaded = 0xFFu;

static u16 *slot_gfx(int s, int slot) {
	if (slot >= GOD_SLOT && slot < FX_SLOT) {
		return obj[s].gfx_base + (GOD_GFX_OFF + (uint32_t)(slot - GOD_SLOT) * 2048u) / 2u;
	}
	if (slot == FX_SLOT) {
		return obj[s].gfx_base + FX_GFX_OFF / 2u;
	}
	return obj[s].gfx_base + (uint32_t)slot * OBJ_BLOCK / 2u;
}

static void put(int s, int slot, const uint8_t *src, uint32_t bytes, int x,
                int y, int w, int h, SpriteSize size, int pal_bank,
                uint8_t flags) {
	u16 *gfx = slot_gfx(s, slot);
	if (obj[s].cache[slot] != src) {
		DC_FlushRange(src, bytes);
		dmaCopy(src, gfx, bytes);
		obj[s].cache[slot] = src;
	}
	const bool off = x <= -w || x >= 256 || y <= -h || y >= 192;
	oamSet(obj[s].oam, slot, off ? 0 : x, off ? 0 : y,
	       (flags & RENDER_SPR_FRONT) ? 0 : 2, pal_bank, size,
	       SpriteColorFormat_16Color, gfx, -1, false, off,
	       (flags & RENDER_SPR_FLIP) != 0u, false, false);
}

static int next_slot(int s) {
	if (obj[s].count >= OBJ_SLOTS) {
		return -1;
	}
	return obj[s].count++;
}

void render_squad(uint8_t civ, uint8_t cls, uint8_t strength, uint8_t anim,
                  uint32_t step, int x, int y, uint8_t flags) {
	if (x <= -32 || x >= 256 || y <= -16 || y >= 192) {
		return; /* culled before it costs a slot */
	}
	const int slot = next_slot(RENDER_TOP);
	if (slot < 0) {
		return;
	}
	const art_obj_t *a = &ART_OBJ[squad_obj(civ, cls)];
	const uint8_t *g = (const uint8_t *)a->gfx;
	if (cls == RENDER_CLS_GENERAL) {
		const art_range_t *r = &ART_SQUAD_GENERAL[anim > 2u ? 2u : anim];
		const uint32_t f = r->first + step % r->count;
		put(RENDER_TOP, slot, g + f * 128u, 128u, x, y, 16, 16,
		    SpriteSize_16x16, PAL_CIV0 + (civ & 3), flags);
		return;
	}
	const art_range_t *r =
	    &ART_SQUAD[strength > 3u ? 3u : strength][anim > 2u ? 2u : anim];
	const uint32_t f = r->first + step % r->count;
	put(RENDER_TOP, slot, g + f * 256u, 256u, x, y, 32, 16, SpriteSize_32x16,
	    PAL_CIV0 + (civ & 3), flags);
}

void render_portrait(render_screen_t s, uint8_t civ, uint8_t cls,
                     uint8_t anim, uint32_t step, int x, int y,
                     uint8_t flags) {
	s = (render_screen_t)(s & 1);
	const int slot = next_slot(s);
	if (slot < 0) {
		return;
	}
	static const art_range_t *const R[RENDER_CLASSES] = {
	    ART_ANIM_SPEAR, ART_ANIM_BLADE, ART_ANIM_ARCHER, ART_ANIM_CAVALRY,
	    ART_ANIM_GENERAL};
	const art_obj_t *a = &ART_OBJ[unit_obj(civ, cls)];
	const art_range_t *r = &R[cls % RENDER_CLASSES][anim % ART_ANIMS];
	const bool big = a->w == 32u;
	const uint32_t bytes = big ? 512u : 128u;
	const uint32_t f = r->first + step % r->count;
	put(s, slot, (const uint8_t *)a->gfx + f * bytes, bytes, x, y,
	    big ? 32 : 16, big ? 32 : 16, big ? SpriteSize_32x32 : SpriteSize_16x16,
	    PAL_CIV0 + (civ & 3), flags);
}

static void small(render_screen_t s, int art, uint32_t frame, int bank, int x,
                  int y, uint8_t flags) {
	s = (render_screen_t)(s & 1);
	const int slot = next_slot(s);
	if (slot < 0) {
		return;
	}
	const art_obj_t *a = &ART_OBJ[art];
	if (frame >= a->frames) {
		frame = 0u;
	}
	put(s, slot, (const uint8_t *)a->gfx + frame * 128u, 128u, x, y, 16, 16,
	    SpriteSize_16x16, bank, flags);
}

void render_icon(render_screen_t s, uint8_t icon, int x, int y, uint8_t flags) {
	small(s, ART_OBJ_ICONS, icon, PAL_ICONS, x, y, flags);
}

void render_pin(render_screen_t s, uint8_t threat, int x, int y, uint8_t flags) {
	small(s, ART_OBJ_PINS, threat & 3u, PAL_PINS, x, y, flags);
}

void render_relic(render_screen_t s, uint8_t god, int x, int y, uint8_t flags) {
	/* relic order: keraunos, trident, helm, spear, harpe = god order   */
	small(s, ART_OBJ_RELICS, god % RENDER_GODS, PAL_RELICS, x, y, flags);
}

static void god_palette(uint8_t god) {
	if (god_pal_loaded == god) {
		return;
	}
	god_pal_loaded = god;
	dmaCopy(ART_OBJ[ART_OBJ_GOD_ARES + GOD_ALPHA[god]].pal,
	        SPRITE_PALETTE + 16 * PAL_GOD, 32);
}

uint8_t render_god_frames(uint8_t anim) {
	return ART_GOD[anim % ART_GOD_ANIMS].count;
}

void render_god(uint8_t god, uint8_t anim, uint32_t step, int x, int y,
                uint8_t flags) {
	if (god >= RENDER_GODS) {
		return;
	}
	god_palette(god);
	const art_range_t *r = &ART_GOD[anim % ART_GOD_ANIMS];
	const uint32_t f = r->first + step % r->count;
	const uint8_t *src = (const uint8_t *)ART_OBJ[ART_OBJ_GOD_ARES + GOD_ALPHA[god]].gfx +
	                     f * 4u * 2048u;
	const bool flip = (flags & RENDER_SPR_FLIP) != 0u;
	for (int q = 0; q < 4; ++q) {
		/* facing left: the quadrant columns swap, each block h-flips */
		const int col = flip ? ((q & 1) ? 0 : 64) : ((q & 1) ? 64 : 0);
		const int row = (q & 2) ? 64 : 0;
		put(RENDER_TOP, GOD_SLOT + q, src + (uint32_t)q * 2048u, 2048u,
		    x + col, y + row, 64, 64, SpriteSize_64x64, PAL_GOD, flags);
	}
	obj[RENDER_TOP].god_drawn = true;
}

void render_fx(uint8_t god, uint8_t frame, int x, int y, uint8_t flags) {
	if (god >= RENDER_GODS) {
		return;
	}
	god_palette(god);
	const uint8_t *src = (const uint8_t *)ART_OBJ[ART_OBJ_FX_ARES + GOD_ALPHA[god]].gfx +
	                     (uint32_t)(frame % ART_FX_FRAMES) * 2048u;
	put(RENDER_TOP, FX_SLOT, src, 2048u, x, y, 64, 64, SpriteSize_64x64,
	    PAL_GOD, flags | RENDER_SPR_FRONT);
	obj[RENDER_TOP].fx_drawn = true;
}

/* ---- overlay recording API ----------------------------------------------- */
int render_text_width(const char *text) {
	const int n = (int)strlen(text);
	return n ? n * 6 - 1 : 0;
}

int render_text(render_screen_t s, int x, int y, uint8_t color,
                const char *text) {
	op_t *p = op_new(s, OP_TEXT);
	if (p) {
		p->x = (int16_t)x;
		p->y = (int16_t)y;
		p->color = color;
		p->str = pool_add(&ov[s & 1], text);
	}
	return x + (int)strlen(text) * 6;
}

int render_text_big(render_screen_t s, int x, int y, uint8_t color,
                    const char *text) {
	op_t *p = op_new(s, OP_BIG);
	if (p) {
		p->x = (int16_t)x;
		p->y = (int16_t)y;
		p->color = color;
		p->str = pool_add(&ov[s & 1], text);
	}
	return x + (int)strlen(text) * 12;
}

int render_text_wrap(render_screen_t s, int x, int y, int w, int lines,
                     uint8_t color, const char *text) {
	op_t *p = op_new(s, OP_WRAP);
	if (p) {
		p->x = (int16_t)x;
		p->y = (int16_t)y;
		p->w = (int16_t)w;
		p->lines = (uint8_t)lines;
		p->color = color;
		p->str = pool_add(&ov[s & 1], text);
	}
	return wrap(x, y, w, lines, color, text, false);
}

static void shape(render_screen_t s, uint8_t type, int x, int y, int w,
                  int h, uint8_t color, uint8_t kind) {
	op_t *p = op_new(s, type);
	if (p) {
		p->x = (int16_t)x;
		p->y = (int16_t)y;
		p->w = (int16_t)w;
		p->h = (int16_t)h;
		p->color = color;
		p->kind = kind;
	}
}

void render_rect(render_screen_t s, int x, int y, int w, int h, uint8_t c) {
	shape(s, OP_RECT, x, y, w, h, c, 0u);
}

void render_box(render_screen_t s, int x, int y, int w, int h, uint8_t c) {
	shape(s, OP_BOX, x, y, w, h, c, 0u);
}

void render_ring(render_screen_t s, int x, int y, int w, int h) {
	shape(s, OP_RING, x, y, w, h, 0u, 0u);
}

void render_panel(render_screen_t s, int x, int y, int w, int h, uint8_t kind) {
	shape(s, OP_PANEL, x, y, w, h, 0u, kind);
}

void render_button(render_screen_t s, int x, int y, int w, int h,
                   const char *label, uint8_t state) {
	shape(s, OP_BUTTON, x, y, w, h, 0u, state);
	overlay_t *o = &ov[s & 1];
	if (o->n_ops > 0u) {
		o->ops[o->n_ops - 1u].str = pool_add(o, label);
	}
}

void render_bar(render_screen_t s, int x, int y, int w, uint32_t num,
                uint32_t den, uint8_t kind) {
	shape(s, OP_BAR, x, y, w, 8, 0u, kind);
	overlay_t *o = &ov[s & 1];
	if (o->n_ops > 0u) {
		o->ops[o->n_ops - 1u].num = num;
		o->ops[o->n_ops - 1u].den = den;
	}
}

uint32_t render_overlay_hash(render_screen_t s) {
	return ov[s & 1].hash_last;
}

/* ---- lifecycle ------------------------------------------------------------ */
void render_init(void) {
	videoSetMode(MODE_0_2D);
	videoSetModeSub(MODE_0_2D);
	vramSetBankA(VRAM_A_MAIN_BG);
	vramSetBankB(VRAM_B_MAIN_SPRITE);
	vramSetBankC(VRAM_C_SUB_BG);
	vramSetBankD(VRAM_D_SUB_SPRITE);
	setBrightness(3, 0);

	font_decode();
	bg_ov[RENDER_TOP] = bgInit(0, BgType_Text4bpp, BgSize_T_256x256, 0, 1);
	bg_ov[RENDER_BOTTOM] = bgInitSub(0, BgType_Text4bpp, BgSize_T_256x256, 0, 1);
	for (int s = 0; s < SCREENS; ++s) {
		bgSetPriority(bg_ov[s], 1);
		u16 *map = bgGetMapPtr(bg_ov[s]);
		for (uint32_t i = 0; i < 1024u; ++i) {
			map[i] = (u16)((i < OV_TILES ? i : 0u) | (OV_BANK << 12));
		}
		bd[s].id = 0xFFu;
		bd[s].god = 0xFFu;
		ov[s].hash_last = 0u;
		ov[s].upload = true; /* first VBlank clears the overlay tiles */
	}
	bg_art[RENDER_TOP] = bgInit(3, BgType_Text8bpp, BgSize_T_256x256, 4, 3);
	bg_art[RENDER_BOTTOM] = bgInitSub(3, BgType_Text8bpp, BgSize_T_256x256, 4, 3);

	oamInit(&oamMain, SpriteMapping_1D_128, false);
	oamInit(&oamSub, SpriteMapping_1D_128, false);
	obj[RENDER_TOP].oam = &oamMain;
	obj[RENDER_TOP].gfx_base = SPRITE_GFX;
	obj[RENDER_BOTTOM].oam = &oamSub;
	obj[RENDER_BOTTOM].gfx_base = SPRITE_GFX_SUB;

	/* OBJ palettes: 4 civs, icons, relics, the UI kit (pins).           */
	for (uint8_t c = 0; c < RENDER_CIVS; ++c) {
		const void *p = ART_OBJ[squad_obj(c, RENDER_CLS_SPEAR)].pal;
		dmaCopy(p, SPRITE_PALETTE + 16 * c, 32);
		dmaCopy(p, SPRITE_PALETTE_SUB + 16 * c, 32);
	}
	dmaCopy(ART_OBJ[ART_OBJ_ICONS].pal, SPRITE_PALETTE + 16 * PAL_ICONS, 32);
	dmaCopy(ART_OBJ[ART_OBJ_ICONS].pal, SPRITE_PALETTE_SUB + 16 * PAL_ICONS, 32);
	dmaCopy(ART_OBJ[ART_OBJ_RELICS].pal, SPRITE_PALETTE + 16 * PAL_RELICS, 32);
	dmaCopy(ART_OBJ[ART_OBJ_RELICS].pal, SPRITE_PALETTE_SUB + 16 * PAL_RELICS, 32);
	dmaCopy(ART_OBJ[ART_OBJ_PINS].pal, SPRITE_PALETTE + 16 * PAL_PINS, 32);
	dmaCopy(ART_OBJ[ART_OBJ_PINS].pal, SPRITE_PALETTE_SUB + 16 * PAL_PINS, 32);
	/* the overlay's bank 15 is the same UI kit palette                  */
	for (int s = 0; s < SCREENS; ++s) {
		memcpy(&pal[s][OV_BANK * 16u], ART_OBJ[ART_OBJ_PINS].pal, 32);
		pal_dirty[s] = true;
	}
}

void render_load_atlas(const void *tiles, const void *map, const void *palp) {
	(void)tiles;
	(void)map;
	(void)palp; /* superseded by the art tables (art_assets.h) */
}

void render_begin(void) {
	for (int s = 0; s < SCREENS; ++s) {
		ov[s].n_ops = 0u;
		ov[s].pool_used = 0u;
		obj[s].count = 0;
		obj[s].god_drawn = false;
		obj[s].fx_drawn = false;
	}
}

/* render_frame(): settle the frame the ui just issued - rasterize changed
 * overlays, hide the sprites nobody drew, step the palette cycles. The
 * VRAM side waits for render_vblank().                                    */
void render_frame(void) {
	frames++;
	for (int s = 0; s < SCREENS; ++s) {
		const uint32_t h = op_hash(&ov[s]);
		if (h != ov[s].hash_last) {
			ov[s].hash_last = h;
			raster(&ov[s]);
			ov[s].upload = true;
		}
		for (int i = obj[s].count; i < obj[s].prev_count; ++i) {
			oamSet(obj[s].oam, i, 0, 0, 0, 0, SpriteSize_8x8,
			       SpriteColorFormat_16Color, NULL, -1, false, true,
			       false, false, false);
		}
		obj[s].prev_count = obj[s].count;
		if (!obj[s].god_drawn) {
			for (int q = 0; q < 4; ++q) {
				oamSet(obj[s].oam, GOD_SLOT + q, 0, 0, 0, 0,
				       SpriteSize_8x8, SpriteColorFormat_16Color,
				       NULL, -1, false, true, false, false, false);
			}
		}
		if (!obj[s].fx_drawn) {
			oamSet(obj[s].oam, FX_SLOT, 0, 0, 0, 0, SpriteSize_8x8,
			       SpriteColorFormat_16Color, NULL, -1, false, true,
			       false, false, false);
		}
		/* water / lava / souls breathe; stages run their own cycles */
		if (bd[s].map && (frames % 12u) == 0u) {
			rotate(pal[s], 1, 3);
			pal_dirty[s] = true;
		} else if (bd[s].id == RENDER_BD_STAGE && bd[s].god < RENDER_GODS) {
			for (int g = 0; g < 3; ++g) {
				const uint8_t *c = STAGE_CYC[bd[s].god][g];
				if (c[2] && (frames % c[2]) == 0u) {
					rotate(pal[s], c[0], c[1]);
					pal_dirty[s] = true;
				}
			}
		}
	}
}

void render_vblank(void) {
	for (int s = 0; s < SCREENS; ++s) {
		if (ov[s].upload) {
			DC_FlushRange(ov[s].canvas, sizeof ov[s].canvas);
			dmaCopy(ov[s].canvas, bgGetGfxPtr(bg_ov[s]),
			        sizeof ov[s].canvas);
			ov[s].upload = false;
		}
		if (pal_dirty[s]) {
			DC_FlushRange(pal[s], sizeof pal[s]);
			dmaCopy(pal[s], s == RENDER_TOP ? BG_PALETTE : BG_PALETTE_SUB,
			        sizeof pal[s]);
			pal_dirty[s] = false;
		}
	}
	if (bd[RENDER_TOP].map) {
		bgSetScroll(bg_art[RENDER_TOP], cam_x, cam_y);
	}
	bgUpdate();
	oamUpdate(&oamMain);
	oamUpdate(&oamSub);
}

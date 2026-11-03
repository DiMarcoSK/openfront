/* artview - War Table art gallery ROM (dual screen, real hardware path).
 *
 * Proves every generated asset on the DS itself, at battle scale:
 *  - 512x512 8bpp battle maps on BG2, scrolled by a high camera;
 *  - armies as 32x16 squad sprites (12 men each), thinning as they lose men;
 *  - the gods as 128x128 entities (4 x 64x64 OAM) that walk, attack, cast,
 *    flinch and die, with their attack effect landing among the squads;
 *  - boss intro stages and screens on BG0, palette cycles on palette RAM.
 * It shares no code with the game modules; it reads only art/ds.
 *
 * Controls: R / A next scene, L / B previous, X pause, D-pad moves the
 * camera on battle maps, Y replays the boss flash. Left alone for 6 s it
 * advances by itself (attract mode), so the gallery also runs as a kiosk.
 */
#include <nds.h>
#include <string.h>

#include "art_assets.h"
#include "art_index.h"
#include "art_palettes.h"

enum { CIV_ASSYRIA, CIV_CARTHAGE, CIV_HELLENES, CIV_ROME, CIVS };
/* class order of the generated ART_OBJ_SQUAD_* / ART_OBJ_UNIT_* enums */
enum { CLS_ARCHER, CLS_BLADE, CLS_CAVALRY, CLS_GENERAL, CLS_SPEAR, CLASSES };
enum { GOD_ZEUS, GOD_POSEIDON, GOD_HADES, GOD_ARES, GOD_KRONOS, GODS };

typedef struct {
	u8 obj, fx, stage, card;
	const u16 *arena_pal;
	u8 cyc[3][3];          /* stage palette cycles: first, count, period */
} god_art_t;

static const god_art_t GOD_ART[GODS] = {
	{ART_OBJ_GOD_ZEUS, ART_OBJ_FX_ZEUS, ART_BG_BOSS_ZEUS, ART_BG_CARD_ZEUS, ART_ARENA_PAL_ZEUS,
	 {{ART_CYCLE_BOSS_ZEUS_EYES, 6}}},
	{ART_OBJ_GOD_POSEIDON, ART_OBJ_FX_POSEIDON, ART_BG_BOSS_POSEIDON, ART_BG_CARD_POSEIDON,
	 ART_ARENA_PAL_POSEIDON, {{ART_CYCLE_BOSS_POSEIDON_EYES, 10}, {ART_CYCLE_BOSS_POSEIDON_SURF, 14}}},
	{ART_OBJ_GOD_HADES, ART_OBJ_FX_HADES, ART_BG_BOSS_HADES, ART_BG_CARD_HADES, ART_ARENA_PAL_HADES,
	 {{ART_CYCLE_BOSS_HADES_SOULFIRE, 9}, {ART_CYCLE_BOSS_HADES_EMBERS, 5}}},
	{ART_OBJ_GOD_ARES, ART_OBJ_FX_ARES, ART_BG_BOSS_ARES, ART_BG_CARD_ARES, ART_ARENA_PAL_ARES,
	 {{ART_CYCLE_BOSS_ARES_FIRE, 4}}},
	{ART_OBJ_GOD_KRONOS, ART_OBJ_FX_KRONOS, ART_BG_BOSS_KRONOS, ART_BG_CARD_KRONOS,
	 ART_ARENA_PAL_KRONOS,
	 {{ART_CYCLE_BOSS_KRONOS_SAND, 5}, {ART_CYCLE_BOSS_KRONOS_EYES, 12},
	  {ART_CYCLE_BOSS_KRONOS_TICKS, 20}}},
};

enum { SC_STILL, SC_FIELD, SC_GOD_INTRO, SC_GOD_FIGHT, SC_PARADE };

typedef struct {
	u8 kind;
	u8 top, bottom;       /* ART_BG_* (god scenes take theirs from GOD_ART) */
	u8 civ_a, civ_b, god;
} scene_t;

static const scene_t SCENES[] = {
	{SC_STILL, ART_BG_TITLE_TOP, ART_BG_TITLE_BOTTOM, 0, 0, 0},
	{SC_FIELD, ART_BG_MAP_PLAINS, ART_BG_BATTLE_BOTTOM_PLAINS, CIV_HELLENES, CIV_ROME, 0},
	{SC_FIELD, ART_BG_MAP_DESERT, ART_BG_BATTLE_BOTTOM_DESERT, CIV_ASSYRIA, CIV_CARTHAGE, 0},
	{SC_FIELD, ART_BG_MAP_HIGHLANDS, ART_BG_BATTLE_BOTTOM_HIGHLANDS, CIV_ROME, CIV_HELLENES, 0},
	{SC_GOD_INTRO, 0, 0, CIV_HELLENES, 0, GOD_ZEUS},
	{SC_GOD_FIGHT, ART_BG_MAP_ARENA, 0, CIV_HELLENES, 0, GOD_ZEUS},
	{SC_GOD_INTRO, 0, 0, CIV_CARTHAGE, 0, GOD_POSEIDON},
	{SC_GOD_FIGHT, ART_BG_MAP_ARENA, 0, CIV_CARTHAGE, 0, GOD_POSEIDON},
	{SC_GOD_INTRO, 0, 0, CIV_ROME, 0, GOD_HADES},
	{SC_GOD_FIGHT, ART_BG_MAP_ARENA, 0, CIV_ROME, 0, GOD_HADES},
	{SC_GOD_INTRO, 0, 0, CIV_HELLENES, 0, GOD_ARES},
	{SC_GOD_FIGHT, ART_BG_MAP_ARENA, 0, CIV_HELLENES, 0, GOD_ARES},
	{SC_GOD_INTRO, 0, 0, CIV_ASSYRIA, 0, GOD_KRONOS},
	{SC_GOD_FIGHT, ART_BG_MAP_ARENA, 0, CIV_ASSYRIA, 0, GOD_KRONOS},
	{SC_PARADE, ART_BG_MAP_PLAINS, ART_BG_RECRUIT_BOTTOM, 0, 0, 0},
	{SC_STILL, ART_BG_BATTLE_MOCK, ART_BG_RESULT_BOTTOM, 0, 0, 0},
};
#define SCENE_COUNT ((int)(sizeof SCENES / sizeof SCENES[0]))

/* ------------------------------------------------------------------------ */
/* actors: everything on the field lives in map coordinates                 */
/* ------------------------------------------------------------------------ */
enum { A_SQUAD, A_GENERAL, A_PORTRAIT, A_FX };

typedef struct {
	u8 kind, cls, anim, step, strength, flip, pal, w, h, alive;
	s16 x, y, vx, front;  /* front: x where the block halts and fights */
	const u8 *sheet;
	u16 *gfx;
	SpriteSize size;
} actor_t;

#define GOD_OAM 124       /* the god's four blocks take the last OAM slots */
#define MAX_ACTORS GOD_OAM
static actor_t actors[MAX_ACTORS];
static int actor_count;

typedef struct {
	int x, y, anim, step, timer, loop, alive;
	u16 *gfx[4];
	actor_t *fx;
} god_t;
static god_t god;

static int bg_still, bg_map, bg_bot;
static int cam_x, cam_y;
static u16 bg_pal[256];
static const scene_t *cur;

#define MAP_W 512
#define MAP_H 512

static const u8 *obj_bytes(int obj) { return (const u8 *)ART_OBJ[obj].gfx; }

static void load_bg(int id, const art_bg_t *a, bool sub, const u16 *pal_override) {
	DC_FlushRange(a->tiles, a->tiles_len);
	DC_FlushRange(a->map, a->map_len);
	dmaCopy(a->tiles, bgGetGfxPtr(id), a->tiles_len);
	dmaCopy(a->map, bgGetMapPtr(id), a->map_len);
	const u16 *pal = pal_override ? pal_override : (const u16 *)a->pal;
	if (sub) {
		dmaCopy(pal, BG_PALETTE_SUB, 512);
	} else {
		memcpy(bg_pal, pal, 512);
		dmaCopy(bg_pal, BG_PALETTE, 512);
	}
}

static actor_t *spawn(int kind, int obj, int pal, int w, int h, SpriteSize size, int x, int y,
                      bool flip) {
	if (actor_count >= MAX_ACTORS)
		return NULL;
	actor_t *a = &actors[actor_count++];
	memset(a, 0, sizeof *a);
	a->kind = kind;
	a->sheet = obj_bytes(obj);
	a->pal = pal;
	a->w = w;
	a->h = h;
	a->size = size;
	a->x = x;
	a->y = y;
	a->flip = flip;
	a->alive = 1;
	a->step = (u8)(x * 7 + y * 3);
	a->gfx = oamAllocateGfx(&oamMain, size, SpriteColorFormat_16Color);
	return a;
}

static int squad_obj(int civ, int cls) { return ART_OBJ_SQUAD_ASSYRIA_ARCHER + civ * CLASSES + cls; }

static void load_civ_palettes(void) {
	for (int civ = 0; civ < CIVS; ++civ)
		dmaCopy(ART_OBJ[squad_obj(civ, CLS_SPEAR)].pal, SPRITE_PALETTE + 16 * civ, 32);
}

/* an army: rows of squads, cavalry on the wing, the general behind */
static void army(int civ, int x0, int y0, int dir, int front, int rows, int cols) {
	static const u8 row_cls[5] = {CLS_SPEAR, CLS_SPEAR, CLS_BLADE, CLS_SPEAR, CLS_ARCHER};
	for (int r = 0; r < rows; ++r) {
		for (int c = 0; c < cols; ++c) {
			int cls = row_cls[r % 5];
			int x = x0 - dir * (c * 34) - dir * (r % 2) * 10;
			actor_t *a = spawn(A_SQUAD, squad_obj(civ, cls), civ, 32, 16, SpriteSize_32x16, x,
			                   y0 + r * 20 + (c % 2) * 3, dir < 0);
			if (!a)
				return;
			a->cls = cls;
			a->vx = dir;
			a->front = front - dir * (c * 34);
			a->anim = ART_SQ_WALK;
		}
	}
	for (int k = 0; k < 4; ++k) {
		actor_t *a = spawn(A_SQUAD, squad_obj(civ, CLS_CAVALRY), civ, 32, 16, SpriteSize_32x16,
		                   x0 - dir * k * 36, y0 + rows * 20 + 12 + (k % 2) * 6, dir < 0);
		if (a) {
			a->cls = CLS_CAVALRY;
			a->vx = dir * 2;
			a->front = front + dir * 20 - dir * k * 36;
			a->anim = ART_SQ_WALK;
		}
	}
	actor_t *g = spawn(A_GENERAL, squad_obj(civ, CLS_GENERAL), civ, 16, 16, SpriteSize_16x16,
	                   x0 - dir * 190, y0 - 26, dir < 0);
	if (g) {
		g->vx = dir;
		g->front = front - dir * 200;
		g->anim = 1;
	}
}

static void god_spawn(void) {
	god.x = MAP_W - 20;
	god.y = 300;
	god.anim = ART_GOD_WALK;
	god.step = god.timer = god.loop = 0;
	god.alive = 1;
}

static void stage_cast(const scene_t *s) {
	actor_count = 0;
	oamClear(&oamMain, 0, 128);
	oamAllocReset(&oamMain);
	memset(&god, 0, sizeof god);
	if (s->kind == SC_FIELD) {
		/* 2 x (25 squads x 12 + 4 horse x 4) = 632 men and two generals */
		army(s->civ_a, 186, 160, 1, 222, 5, 5);
		army(s->civ_b, 294, 160, -1, 258, 5, 5);
		cam_x = 128;
		cam_y = 130;
	} else if (s->kind == SC_GOD_FIGHT) {
		army(s->civ_a, 270, 196, 1, 300, 4, 5);
		for (int q = 0; q < 4; ++q)
			god.gfx[q] = oamAllocateGfx(&oamMain, SpriteSize_64x64, SpriteColorFormat_16Color);
		god.fx = spawn(A_FX, GOD_ART[s->god].fx, ART_PAL_GOD, 64, 64, SpriteSize_64x64, 0, 0, true);
		if (god.fx)
			god.fx->alive = 0;
		dmaCopy(ART_OBJ[GOD_ART[s->god].obj].pal, SPRITE_PALETTE + 16 * ART_PAL_GOD, 32);
		god_spawn();
		cam_x = 256;
		cam_y = 150;
	} else if (s->kind == SC_PARADE) {
		/* the close-up portraits (menus, cards) cycling every animation */
		for (int civ = 0; civ < CIVS; ++civ) {
			for (int cls = 0; cls < CLASSES; ++cls) {
				int big = (cls == CLS_CAVALRY || cls == CLS_GENERAL);
				actor_t *a = spawn(A_PORTRAIT, ART_OBJ_UNIT_ASSYRIA_ARCHER + civ * CLASSES + cls, civ,
				                   big ? 32 : 16, big ? 32 : 16,
				                   big ? SpriteSize_32x32 : SpriteSize_16x16,
				                   10 + cls * 48 + (big ? 0 : 8), 44 + civ * 36 + (big ? 0 : 14),
				                   false);
				if (a)
					a->cls = cls;
			}
		}
		cam_x = cam_y = 0;
	}
}

static const art_range_t *portrait_range(const actor_t *a) {
	static const art_range_t *const R[CLASSES] = {ART_ANIM_ARCHER, ART_ANIM_BLADE, ART_ANIM_CAVALRY,
	                                              ART_ANIM_GENERAL, ART_ANIM_SPEAR};
	return &R[a->cls][a->anim];
}

static void fx_at(int x, int y) {
	actor_t *f = god.fx;
	if (!f)
		return;
	f->x = x;
	f->y = y;
	f->step = 0;
	f->alive = 1;
}

/* the god's effect thins every squad it lands on */
static void fx_hits(void) {
	actor_t *f = god.fx;
	for (int i = 0; i < actor_count; ++i) {
		actor_t *a = &actors[i];
		if (a->kind != A_SQUAD || !a->alive)
			continue;
		int dx = a->x + 16 - (f->x + 32), dy = a->y + 8 - (f->y + 48);
		if (dx * dx + dy * dy < 44 * 44) {
			if (a->strength < 3)
				a->strength++;
			else
				a->alive = 0;
		}
	}
}

/* walk -> attack (effect lands) -> idle -> ... -> special; hurt now and
 * then; after six exchanges the god dies and a new one walks in. */
static void tick_god(bool advance) {
	const art_range_t *r = &ART_GOD[god.anim];
	if (advance) {
		god.step++;
		god.timer++;
		switch (god.anim) {
		case ART_GOD_WALK:
			god.x -= 3;
			if (god.timer > 16 || god.x < 430) {
				god.anim = (god.loop % 3 == 2) ? ART_GOD_SPECIAL : ART_GOD_ATTACK;
				god.step = god.timer = 0;
			}
			break;
		case ART_GOD_ATTACK:
		case ART_GOD_SPECIAL:
			if (god.step == 3)
				fx_at(god.x - 150 - (god.anim == ART_GOD_SPECIAL ? 30 : 0), god.y - 90);
			if (god.step >= r->count) {
				god.anim = (++god.loop % 4 == 3) ? ART_GOD_HURT : ART_GOD_IDLE;
				god.step = god.timer = 0;
			}
			break;
		case ART_GOD_HURT:
			if (god.step >= 4) {
				god.anim = ART_GOD_IDLE;
				god.step = god.timer = 0;
			}
			break;
		case ART_GOD_IDLE:
			if (god.loop >= 6) {
				god.anim = ART_GOD_DEATH;
			} else if (god.timer > 6) {
				god.anim = ART_GOD_WALK;
			} else {
				break;
			}
			god.step = god.timer = 0;
			break;
		case ART_GOD_DEATH:
			if (god.step >= r->count + 8) {
				stage_cast(cur);
				god_spawn();
				return;
			}
			break;
		}
		r = &ART_GOD[god.anim];
	}
	bool gone = god.anim == ART_GOD_DEATH && god.step >= r->count;
	int frame = r->first + (gone ? r->count - 1 : god.step % r->count);
	const u8 *src = obj_bytes(GOD_ART[cur->god].obj) + frame * 4 * 2048;
	for (int q = 0; q < 4; ++q) {
		dmaCopy(src + q * 2048, god.gfx[q], 2048);
		/* facing left: the quadrant columns swap and each block h-flips */
		int qx = (q & 1) ? 0 : 64, qy = (q & 2) ? 64 : 0;
		int sx = god.x - 64 + qx - cam_x, sy = god.y - 128 + qy - cam_y;
		bool off = gone || sx < -64 || sx > 255 || sy < -64 || sy > 191;
		oamSet(&oamMain, GOD_OAM + q, off ? 0 : sx, off ? 0 : sy, 1, ART_PAL_GOD, SpriteSize_64x64,
		       SpriteColorFormat_16Color, god.gfx[q], -1, false, off, true, false, false);
	}
}

static void tick_actors(bool advance) {
	static const art_range_t fx_range = {0, ART_FX_FRAMES};
	for (int i = 0; i < actor_count; ++i) {
		actor_t *a = &actors[i];
		const art_range_t *r;
		int bytes = a->w * a->h / 2;
		if (a->kind == A_SQUAD) {
			if (advance && a->alive) {
				a->step++;
				bool engaged = (a->vx > 0 && a->x >= a->front) || (a->vx < 0 && a->x <= a->front);
				if (cur->kind == SC_GOD_FIGHT)
					engaged = engaged || (god.alive && a->x + 150 > god.x);
				if (!engaged)
					a->x += a->vx;
				a->anim = engaged ? ART_SQ_ATTACK : ART_SQ_WALK;
				/* attrition while engaged: a block loses men over time */
				if (engaged && cur->kind == SC_FIELD && (a->step % 61) == 0 && a->strength < 3)
					a->strength++;
			}
			r = &ART_SQUAD[a->strength][a->anim];
		} else if (a->kind == A_GENERAL) {
			if (advance) {
				a->step++;
				bool there = (a->vx > 0 && a->x >= a->front) || (a->vx < 0 && a->x <= a->front);
				if (!there)
					a->x += a->vx;
				a->anim = there ? 2 : 1;
			}
			r = &ART_SQUAD_GENERAL[a->anim];
		} else if (a->kind == A_FX) {
			if (advance && a->alive) {
				if (a->step == 1)
					fx_hits();
				if (++a->step >= ART_FX_FRAMES)
					a->alive = 0;
			}
			r = &fx_range;
		} else {
			if (advance) {
				const art_range_t *pr = portrait_range(a);
				if ((++a->step % (pr->count * 3)) == 0) {
					a->anim = (a->anim + 1) % ART_ANIMS;
					if (a->anim == ART_COMMAND && a->cls != CLS_GENERAL)
						a->anim = ART_HURT;
				}
			}
			r = portrait_range(a);
		}
		int frame = r->first + (a->kind == A_FX ? (a->step < ART_FX_FRAMES ? a->step : ART_FX_FRAMES - 1)
		                                        : a->step % r->count);
		dmaCopy(a->sheet + frame * bytes, a->gfx, bytes);
		int sx = a->x - cam_x, sy = a->y - cam_y;
		bool off = !a->alive || sx < -a->w || sx > 255 || sy < -a->h || sy > 191;
		oamSet(&oamMain, i, off ? 0 : sx, off ? 0 : sy, a->kind == A_FX ? 0 : 2, a->pal, a->size,
		       SpriteColorFormat_16Color, a->gfx, -1, false, off, a->flip, false, false);
	}
}

static void rotate(u16 *pal, int first, int count) {
	u16 last = pal[first + count - 1];
	for (int i = first + count - 1; i > first; --i)
		pal[i] = pal[i - 1];
	pal[first] = last;
}

static bool on_map(const scene_t *s) {
	return s->kind == SC_FIELD || s->kind == SC_GOD_FIGHT || s->kind == SC_PARADE;
}

static void show(int idx) {
	const scene_t *s = &SCENES[idx];
	cur = s;
	if (on_map(s)) {
		bgHide(bg_still);
		load_bg(bg_map, &ART_BG[s->top], false,
		        s->kind == SC_GOD_FIGHT ? GOD_ART[s->god].arena_pal : NULL);
		bgShow(bg_map);
	} else {
		bgHide(bg_map);
		int top = s->kind == SC_GOD_INTRO ? GOD_ART[s->god].stage : s->top;
		load_bg(bg_still, &ART_BG[top], false, NULL);
		bgShow(bg_still);
	}
	bool godly = s->kind == SC_GOD_INTRO || s->kind == SC_GOD_FIGHT;
	load_bg(bg_bot, &ART_BG[godly ? GOD_ART[s->god].card : s->bottom], true, NULL);
	stage_cast(s);
}

int main(void) {
	videoSetMode(MODE_0_2D);
	videoSetModeSub(MODE_0_2D);
	vramSetBankA(VRAM_A_MAIN_BG);
	vramSetBankB(VRAM_B_MAIN_SPRITE);
	vramSetBankC(VRAM_C_SUB_BG);

	/* BG0: 256x256 stills (map 0-2 KB, tiles 16-64 KB);
	 * BG2: the 512x512 battle map (map 8-16 KB, tiles 64-128 KB) */
	bg_still = bgInit(0, BgType_Text8bpp, BgSize_T_256x256, 0, 1);
	bg_map = bgInit(2, BgType_Text8bpp, BgSize_T_512x512, 4, 4);
	bg_bot = bgInitSub(0, BgType_Text8bpp, BgSize_T_256x256, 0, 1);
	/* sprites draw over the field: BGs at the back (priority 3) */
	bgSetPriority(bg_still, 3);
	bgSetPriority(bg_map, 3);
	oamInit(&oamMain, SpriteMapping_1D_128, false);
	load_civ_palettes();

#ifndef START_SCENE
#define START_SCENE 0
#endif
	int scene = START_SCENE, frame = 0, flash = 0, idle = 0;
	bool paused = false;
	show(scene);

	for (;;) {
		swiWaitForVBlank();
		oamUpdate(&oamMain);
		bgUpdate();

		scanKeys();
		u32 down = keysDown(), held = keysHeld();
		u32 pad = KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT;
		if (down || (held & pad))
			idle = 0;
		else if (++idle >= 360) {
			idle = 0;
			down = KEY_R;
		}
		if (down & (KEY_R | KEY_A | KEY_L | KEY_B)) {
			scene = (down & (KEY_R | KEY_A)) ? (scene + 1) % SCENE_COUNT
			                                 : (scene + SCENE_COUNT - 1) % SCENE_COUNT;
			show(scene);
			flash = SCENES[scene].kind == SC_GOD_INTRO ? 24 : 0;
		}
		if (down & KEY_X)
			paused = !paused;
		if (down & KEY_Y)
			flash = 24;

		++frame;
		bool adv = !paused && (frame % 8) == 0;

		/* camera: d-pad, else follow the god or drift along the front */
		if (held & KEY_LEFT) cam_x -= 3;
		if (held & KEY_RIGHT) cam_x += 3;
		if (held & KEY_UP) cam_y -= 3;
		if (held & KEY_DOWN) cam_y += 3;
		if (!(held & pad) && !paused) {
			if (cur->kind == SC_GOD_FIGHT && god.alive)
				cam_x += ((god.x - 210) - cam_x) / 16;
			else if (cur->kind == SC_FIELD && (frame % 3) == 0)
				cam_x += ((frame / 480) % 2) ? -1 : 1;
		}
		if (on_map(cur)) {
			if (cam_x < 0) cam_x = 0;
			if (cam_y < 0) cam_y = 0;
			if (cam_x > MAP_W - 256) cam_x = MAP_W - 256;
			if (cam_y > MAP_H - 192) cam_y = MAP_H - 192;
			bgSetScroll(bg_map, cam_x, cam_y);
		}

		tick_actors(adv);
		if (cur->kind == SC_GOD_FIGHT)
			tick_god(adv);

		/* water / lava / souls breathe on the maps; stage cycles on intros */
		if (!paused && cur->kind != SC_STILL && cur->kind != SC_GOD_INTRO && (frame % 12) == 0) {
			rotate(bg_pal, 1, 3);
			dmaCopy(bg_pal, BG_PALETTE, 512);
		}
		if (cur->kind == SC_GOD_INTRO && !paused) {
			bool dirty = false;
			for (int g = 0; g < 3; ++g) {
				const u8 *c = GOD_ART[cur->god].cyc[g];
				if (c[2] && (frame % c[2]) == 0) {
					rotate(bg_pal, c[0], c[1]);
					dirty = true;
				}
			}
			if (dirty)
				dmaCopy(bg_pal, BG_PALETTE, 512);
			if (cur->god == GOD_ZEUS && (frame % 300) == 0)
				flash = 10;
		}
		if (flash > 0) {
			setBrightness(1, flash > 6 ? 12 : flash * 2);
			if (--flash == 0)
				setBrightness(1, 0);
		}
	}
}

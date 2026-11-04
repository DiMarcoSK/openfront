/* battle.c - top battlefield + bottom command panel (B10, spec-04 R1-R12).
 * ADR-0008: the battle runs in real time. The top screen is the field the
 * sim moves - every division at its own x and lane, marching, wheeling
 * round a wing, fighting, shooting or running - under the high camera
 * that follows the clash; the god of a divine front stands behind its
 * host. The bottom screen commands: four stances (d-pad == taps), Undo,
 * Pause, speed, the general's Rally / Charge, the live minimap, morale,
 * flanks and the god's countdown. The order panel and the pause hold the
 * field (0 ticks while open). The copy rows stay the semantic channel
 * (host tests); the sealed frame is never redrawn (R11) - ui.c switches
 * to Result in <= 1 frame.
 */
#include <nds.h>
#include <stdio.h>
#include <string.h>

#include "contracts/sim_api.h"
#include "contracts/queue.h"
#include "contracts/audio.h"
#include "contracts/render.h"
#include "contracts/ui.h"

#include "ui_draw.h"
#include "screens.h"
#include "strings.h"

static uint32_t order_seq_stamp;

/* B22: the reading surfaces. All three hold the tick and the field.    */
static bool panel_open;
static bool paused;
static int intro_frames;           /* > 0 while the boss intro plays     */
#define INTRO_FRAMES 120           /* 2 s at 60 Hz (B30: <= 2 s)          */

/* the view's memory of the sim, to animate what changed               */
static int god_anim, god_step, god_timer;
static int fx_frame = -1, fx_x, fx_y;
static int flash;
static int cam_x = 128;

/* the ground and the fallen (GAME_DESIGN 4/7): every squad, corpse and
 * general stands on its lane's path round rock and water (render reads
 * art_terrain.h); a company that falls leaves its dead where it fell -
 * struck, falling, down - so losses read on the field, not only in a bar */
#define FALLEN_MAX 32
typedef struct {
	int16_t x, y;            /* map px: block centre, sprite top row    */
	uint8_t civ, cls, flip, age;
} fallen_t;
static fallen_t fallen[FALLEN_MAX];
static uint8_t fallen_next, fallen_count;
static uint8_t seen_companies[2][SIM_DIVISIONS];
static uint8_t field_map = RENDER_BD_MAP_PLAINS;

/* GAME_DESIGN 7 contextual alerts: each new sim event shows once on the
 * top plate for 2 s with the alert cue; the field acts it out.          */
#define ALERT_FRAMES 120
static uint16_t seen_event_seq;
static int alert_frames;
static uint8_t alert_event;

bool battle_holds(void) {
	return panel_open || paused || intro_frames > 0;
}

/* the god on this field: a front's own, or the one a wrath brought     */
static uint8_t battle_god(void) {
	const uint8_t b = sim_battle_boss();
	return (b < SIM_BOSSES) ? b : (uint8_t)RENDER_GOD_NONE;
}

/* the host across the field: rebels wear our own colours              */
static uint8_t foe_civ(const front_art_t *a) {
	return sim_battle_kind() == SIM_WAR_REBELLION ? (uint8_t)RENDER_CIV_HELLENES : a->enemy;
}

void battle_enter(bool intro) {
	intro_frames = (intro && battle_god() != RENDER_GOD_NONE) ? INTRO_FRAMES : 0;
	panel_open = false;
	paused = false;
	god_anim = RENDER_GOD_WALK;
	god_step = god_timer = 0;
	fx_frame = -1;
	flash = 0;
	cam_x = 128;
	seen_event_seq = sim_battle_event_seq();
	alert_frames = 0;
	alert_event = SIM_EVT_NONE;
	fallen_next = fallen_count = 0u;
	for (uint8_t side = 0; side < 2u; ++side) {
		uint8_t n = 0u;
		const sim_division_t *d = sim_battle_divisions(side, &n);
		memset(seen_companies[side], 0, sizeof seen_companies[side]);
		for (uint8_t i = 0; i < n && i < SIM_DIVISIONS; ++i) {
			seen_companies[side][i] = d[i].companies;
		}
	}
}

/* ---- the five written states (spec-04 T030) ----------------------------- */
enum { BST_EMPTY = 0, BST_LOADING, BST_ERROR, BST_SUCCESS, BST_PARTIAL };

uint8_t battle_state_id(void);
const char *battle_state_copy(void);

uint8_t battle_state_id(void) {
	if (!sim_battle_active() && !sim_battle_sealed()) {
		return BST_EMPTY;
	}
	if (sim_battle_time() == 0u) {
		return BST_LOADING;
	}
	const char *st = sim_order_status();
	if (st != NULL && (strstr(st, "ready in") || strstr(st, "need"))) {
		return BST_ERROR;
	}
	if (sim_battle_units() < sim_battle_units_start()) {
		return BST_PARTIAL;
	}
	return BST_SUCCESS;
}

const char *battle_state_copy(void) {
	static char copy[64];
	switch (battle_state_id()) {
	case BST_EMPTY:
		return "No battle running. Deploy from Base.";
	case BST_LOADING:
		snprintf(copy, sizeof copy, "Deploying %u companies. The lines advance.",
		         (unsigned)sim_battle_units());
		return copy;
	case BST_ERROR:
		return sim_order_status();
	case BST_PARTIAL:
		snprintf(copy, sizeof copy, "%u of %u companies standing.",
		         (unsigned)sim_battle_units(),
		         (unsigned)sim_battle_units_start());
		return copy;
	default:
		snprintf(copy, sizeof copy, "All %u companies standing.",
		         (unsigned)sim_battle_units());
		return copy;
	}
}

/* mm:ss of field time                                                   */
static void clock_text(char *buf, size_t n, uint16_t ticks) {
	const unsigned s = ticks / SIM_BATTLE_HZ;
	snprintf(buf, n, "%u:%02u", s / 60u, s % 60u);
}

/* B21 (T020): the bottom hit map on the command panel art:
 * header speed chip (py < 22, px >= 184), minimap = order panel toggle
 * (px < 132), 2x2 stance tiles at x 134/194, y 26/60 (58 x 30), the
 * Pause bar y 94..117, Undo chip y 120..139 x < 194, speed chip x >= 194,
 * the general's Rally / Charge y 140..159 (8 / 9).
 * -1 = outside (tap ignored, input preserved).                          */
static int touch_target(uint32_t value) {
	const uint32_t px = (value >> 8) & 0xFFu;
	const uint32_t py = value & 0xFFu;
	if (py < 22u) {
		return (px >= 184u) ? 6 : -1;
	}
	if (py >= 160u) {
		return -1;
	}
	if (px < 132u) {
		return (py < 154u) ? 7 : -1; /* minimap: the order panel   */
	}
	if (py < 92u) {
		const uint32_t col = (px < 194u) ? 0u : 1u;
		const uint32_t row = (py < 60u) ? 0u : 1u;
		return (int)(row * 2u + col); /* ADV FLK / HOL FAL */
	}
	if (py < 118u) {
		return 4; /* the Pause bar                                     */
	}
	if (py < 140u) {
		return (px < 194u) ? 5 : 6;
	}
	return (px < 194u) ? 8 : 9; /* the general: Rally / Charge     */
}

static void send_cmd(uint8_t code, uint8_t arg) {
	sim_cmd_t cmd;
	memset(&cmd, 0, sizeof cmd);
	cmd.seq = ++order_seq_stamp;
	cmd.cmd = code;
	cmd.arg = arg;
	sim_enqueue(&cmd);
}

static void cycle_speed(void) {
	uint8_t next = sim_speed_request() * 2u;
	if (next > SIM_SPEED_X4) {
		next = SIM_SPEED_X1;
	}
	sim_set_speed(next);
	audio_cue(AUDIO_CUE_SPEED); /* id only, drop if unavailable          */
}

/* tile index (touch order) -> stance                                     */
static const uint8_t TILE_ORDER[4] = {
	SIM_ORDER_ADVANCE, SIM_ORDER_FLANK, SIM_ORDER_HOLD, SIM_ORDER_FALLBACK,
};

void battle_input(uint8_t kind, uint32_t value) {
	if (intro_frames > 0) {
		/* the intro is a reading surface: any A/B/tap skips it      */
		if (kind == (uint8_t)UI_EVT_TOUCH ||
		    (value & (KEY_A | KEY_B | KEY_START))) {
			intro_frames = 0;
		}
		return;
	}
	/* touch direct-select: every target is one tap; nothing on this
	 * panel is irreversible (a stance has Undo, a pause resumes).       */
	if (kind == (uint8_t)UI_EVT_TOUCH) {
		const int t = touch_target(value);
		if (t < 0) {
			return;
		}
		if (t == 7) {
			panel_open = !panel_open; /* B22: HP tap-expand         */
			return;
		}
		if (t == 4) {
			paused = !paused;
			return;
		}
		if (panel_open) {
			return; /* the panel reads; orders wait for it to close */
		}
		if (t <= 3) {
			send_cmd(7u, TILE_ORDER[t]);
		} else if (t == 5) {
			send_cmd(9u, 0u);
		} else if (t == 6) {
			cycle_speed();
		} else {
			send_cmd(10u, t == 8 ? SIM_ABILITY_RALLY : SIM_ABILITY_CHARGE);
		}
		return;
	}
	if (kind != (uint8_t)UI_EVT_KEY) {
		return;
	}
	/* D-pad sets the stance, A pauses, B undoes the last stance, R
	 * cycles speed (B20), X opens/closes the order panel (B22, the HP
	 * handle), Y rallies and L charges (the general, GAME_DESIGN 7).
	 * Every path reuses the touch handler's commands (full parity).    */
	const uint32_t keys = value;
	if (keys & KEY_X) {
		panel_open = !panel_open;
		return;
	}
	if (keys & KEY_A) {
		paused = !paused;
		return;
	}
	if (panel_open) {
		if (keys & KEY_B) {
			panel_open = false; /* B backs out of the reading panel */
		}
		return;
	}
	if (keys & KEY_UP) {
		send_cmd(7u, SIM_ORDER_ADVANCE);
	} else if (keys & KEY_LEFT) {
		send_cmd(7u, SIM_ORDER_HOLD);
	} else if (keys & KEY_RIGHT) {
		send_cmd(7u, SIM_ORDER_FLANK);
	} else if (keys & KEY_DOWN) {
		send_cmd(7u, SIM_ORDER_FALLBACK);
	} else if (keys & KEY_B) {
		send_cmd(9u, 0u);
	} else if (keys & KEY_R) {
		cycle_speed();
	} else if (keys & (KEY_Y | KEY_L)) {
		send_cmd(10u, (keys & KEY_Y) ? SIM_ABILITY_RALLY : SIM_ABILITY_CHARGE);
	}
}

/* ---- art: the field ------------------------------------------------------- */
#define CAM_Y 150    /* the high camera's fixed tilt over the field      */
static const int LANE_Y[SIM_LANES] = {186, 228, 270};
static const uint8_t SPRITE_CLS[3] = {
	RENDER_CLS_SPEAR, RENDER_CLS_ARCHER, RENDER_CLS_CAVALRY,
};

static void god_tick(uint8_t god, int gx, bool advance) {
	if (!advance) {
		return;
	}
	god_step++;
	god_timer++;
	const int n = render_god_frames((uint8_t)god_anim);
	switch (god_anim) {
	case RENDER_GOD_WALK:
		if (god_timer > 12) {
			god_anim = RENDER_GOD_IDLE;
			god_step = god_timer = 0;
		}
		break;
	case RENDER_GOD_ATTACK:
	case RENDER_GOD_SPECIAL:
		if (god_step == 3) {
			fx_frame = 0;
			fx_x = gx - 120;
			fx_y = LANE_Y[SIM_LANE_CENTRE] - 20;
			if (god == RENDER_GOD_ZEUS) {
				flash = 10; /* the keraunos lights the field    */
			}
		}
		if (god_step >= n) {
			god_anim = RENDER_GOD_IDLE;
			god_step = god_timer = 0;
		}
		break;
	case RENDER_GOD_HURT:
		if (god_step >= 4) {
			god_anim = RENDER_GOD_IDLE;
			god_step = god_timer = 0;
		}
		break;
	case RENDER_GOD_DEATH:
		break;
	default:
		if (god_timer > 30) { /* a divine front never sits still    */
			god_anim = RENDER_GOD_SPECIAL;
			god_step = god_timer = 0;
		}
		break;
	}
}

/* alert tone: losses and the god's blows read in the crit colour,
 * omens in warn, the general's acts and enemy breaks in accent         */
static uint8_t alert_color(uint8_t evt) {
	switch (evt) {
	case SIM_EVT_BOSS_OMEN:
	case SIM_EVT_WAVERING:
	case SIM_EVT_CAV_CHARGE:
		return RENDER_WARN_L;
	case SIM_EVT_FLANK_BROKEN:
	case SIM_EVT_AMBUSH:
	case SIM_EVT_BOSS_STRIKE:
	case SIM_EVT_BOSS_PHASE:
	case SIM_EVT_COLLAPSE:
		return RENDER_CRIT_L;
	default:
		return RENDER_ACCENT_L;
	}
}

/* a new sim event: cue it, and let the field act it out                */
static void alert_take(uint8_t god) {
	if (sim_battle_event_seq() == seen_event_seq) {
		return;
	}
	seen_event_seq = sim_battle_event_seq();
	alert_event = sim_battle_event();
	alert_frames = ALERT_FRAMES;
	audio_cue(alert_color(alert_event) == RENDER_ACCENT_L ? AUDIO_CUE_CONFIRM
	                                                    : AUDIO_CUE_ALERT);
	if (god != RENDER_GOD_NONE &&
	    (alert_event == SIM_EVT_BOSS_STRIKE || alert_event == SIM_EVT_BOSS_OMEN ||
	     alert_event == SIM_EVT_BOSS_PHASE)) {
		god_anim = RENDER_GOD_SPECIAL; /* the god casts it            */
		god_step = god_timer = 0;
	} else if (god != RENDER_GOD_NONE && alert_event == SIM_EVT_BOSS_COUNTERED) {
		god_anim = RENDER_GOD_HURT;
		god_step = god_timer = 0;
	}
	if (god == RENDER_GOD_ZEUS && alert_event == SIM_EVT_BOSS_STRIKE) {
		flash = 12; /* the thunderbolt lands                            */
	}
}

/* the row a lane walks at map x: bent round rock and water           */
static int lane_y(uint8_t lane, int x) {
	lane %= SIM_LANES;
	return LANE_Y[lane] + render_lane_dy(field_map, lane, x);
}

/* a division is drawn inside the ground its lane can walk: a mountain
 * wall or the arena chasm stops the picture where the field ends       */
static int lane_clamp(uint8_t lane, int x, int margin) {
	int x0, x1;
	render_lane_span(field_map, lane % SIM_LANES, &x0, &x1);
	return x < x0 + margin ? x0 + margin : x > x1 - margin ? x1 - margin : x;
}

static bool lane_past_end(uint8_t lane, int x) {
	int x0, x1;
	render_lane_span(field_map, lane % SIM_LANES, &x0, &x1);
	return x < x0 || x > x1;
}

/* the blocks a division mustered as - fixed for the battle, so losses
 * thin the blocks instead of quietly removing them                     */
static int blocks_for(const sim_division_t *d, int cap) {
	const unsigned c = d->companies_start;
	const int n = c >= 6u ? 3 : c >= 3u ? 2 : 1;
	return n > cap ? cap : n;
}

/* men still up in a division drawn as k blocks of 12                   */
static int men_up(const sim_division_t *d, int k) {
	if (d->companies_start == 0u || d->companies == 0u) {
		return 0;
	}
	const unsigned start = d->companies_start;
	return (int)((12u * (unsigned)k * d->companies + start - 1u) / start);
}

/* the art strength of block j (front first): 12/9/6/3 men, -1 = gone   */
static int block_strength(int men, int j) {
	const int left = men - 12 * j;
	return left <= 0 ? -1 : left >= 12 ? 0 : left >= 9 ? 1 : left >= 6 ? 2 : 3;
}

static uint8_t div_anim(const sim_division_t *d) {
	switch (d->state) {
	case SIM_DIV_FIGHT:
	case SIM_DIV_SHOOT: return RENDER_ANIM_ATTACK;
	case SIM_DIV_HOLD:  return RENDER_ANIM_IDLE;
	default:            return RENDER_ANIM_WALK;
	}
}

static void fall(int x, int y, uint8_t civ, uint8_t cls, uint8_t flip) {
	fallen_t *f = &fallen[fallen_next];
	/* no two heaps quite alike: a few px of scatter per body           */
	f->x = (int16_t)(x + (int)(fallen_next * 7u % 9u) - 4);
	f->y = (int16_t)(y + (int)(fallen_next * 5u % 5u) - 2);
	f->civ = civ;
	f->cls = cls;
	f->flip = flip;
	f->age = 0u;
	fallen_next = (uint8_t)((fallen_next + 1u) % FALLEN_MAX);
	if (fallen_count < FALLEN_MAX) {
		fallen_count++;
	}
}

/* one side's divisions: blocks of 12 at the division's x on its lane's
 * path; every company lost since the last frame drops where it stood   */
static void paint_side(uint8_t side, uint8_t civ, uint32_t step, int cy) {
	uint8_t n = 0u;
	const sim_division_t *d = sim_battle_divisions(side, &n);
	const int cap = (side == SIM_SIDE_OURS) ? 3 : 2;
	const int face = (side == SIM_SIDE_OURS) ? 1 : -1;
	const uint8_t flip = (side == SIM_SIDE_FOE) ? RENDER_SPR_FLIP : 0u;
	for (uint8_t i = 0; i < n && i < SIM_DIVISIONS; ++i) {
		const bool rout = d[i].state == SIM_DIV_ROUT;
		const int k = blocks_for(&d[i], cap);
		const int men = men_up(&d[i], k);
		const uint8_t lane = d[i].lane % SIM_LANES;
		/* blocks run back from the leading x: ours west, theirs east   */
		const int dx = lane_clamp(lane, d[i].x, 16 + 12 * (k - 1));
		const int jit = (int)(i % 3u) * 4 - 4;
		/* the riders in a wheel ride between the lanes                */
		int wheel = 0;
		if (d[i].state == SIM_DIV_WHEEL) {
			wheel = (lane == SIM_LANE_CENTRE) ? 0 : (lane == SIM_LANE_NORTH ? 20 : -20);
		}
		/* the fallen: a company down = a heap where the rear block was */
		const uint8_t was = seen_companies[side][i];
		const uint8_t now = d[i].companies;
		/* tick 0 is the muster: the view may still hold the last
		 * battle's numbers, and nobody has fallen yet                  */
		if (now < was && !rout && sim_battle_time() > 0u &&
		    !lane_past_end(lane, d[i].x)) {
			const int j = men > 0 ? (men - 1) / 12 : 0;
			const int bx = dx - 12 * j * face;
			const int heaps = (was - now) > 2 ? 2 : (was - now);
			for (int h = 0; h < heaps; ++h) {
				fall(bx + h * 10 * face, lane_y(lane, bx) + jit + 2,
				     civ, SPRITE_CLS[d[i].cls % 3u], flip);
			}
		}
		seen_companies[side][i] = now;
		if (d[i].state == SIM_DIV_DEAD || now == 0u ||
		    (rout && lane_past_end(lane, d[i].x))) {
			continue;
		}
		for (int s = 0; s < k; ++s) {
			const int st = block_strength(men, s);
			if (st < 0) {
				break;
			}
			const int cx = dx - s * 12 * face;
			const int y = lane_y(lane, cx) + jit + wheel + (s & 1) * 6;
			render_squad(civ, SPRITE_CLS[d[i].cls % 3u], (uint8_t)st,
			             div_anim(&d[i]), step + i * 3u + (uint32_t)s,
			             cx - 16 - cam_x, y - cy, rout ? 0u : flip);
		}
		/* a division struck in the side or the rear wears a crit tick
		 * (a shape, not only a colour: constitution 7)               */
		if (d[i].flags & SIM_DIV_FLANKED) {
			render_rect(RENDER_TOP, dx - cam_x - 2, lane_y(lane, dx) + jit - cy - 6, 5, 5,
			            RENDER_CRIT_L);
		}
	}
}

/* the dead lie under the living (issued after them: a later OAM slot
 * draws behind); struck and falling play once, then they stay down     */
static void paint_fallen(bool tick, int cy) {
	for (uint8_t k = 0; k < fallen_count; ++k) {
		fallen_t *f = &fallen[k];
		const uint8_t frame = f->age < 8u ? 0u : f->age < 16u ? 1u : 2u;
		render_squad(f->civ, f->cls, 0u, RENDER_ANIM_DEATH, frame,
		             f->x - 16 - cam_x, f->y - cy, f->flip);
		if (tick && f->age < 255u) {
			f->age++;
		}
	}
}

/* the camera follows the clash: midway between the two leading edges   */
static int front_mid(void) {
	uint8_t n0 = 0u, n1 = 0u;
	const sim_division_t *o = sim_battle_divisions(SIM_SIDE_OURS, &n0);
	const sim_division_t *f = sim_battle_divisions(SIM_SIDE_FOE, &n1);
	int lead = 0, rear = 512;
	for (uint8_t i = 0; i < n0; ++i) {
		if (o[i].state != SIM_DIV_DEAD && o[i].x > lead) lead = o[i].x;
	}
	for (uint8_t i = 0; i < n1; ++i) {
		if (f[i].state != SIM_DIV_DEAD && f[i].x < rear) rear = f[i].x;
	}
	if (lead == 0) lead = 160;
	if (rear == 512) rear = 352;
	return (lead + rear) / 2;
}

static int foe_rear(void) {
	uint8_t n = 0u;
	const sim_division_t *f = sim_battle_divisions(SIM_SIDE_FOE, &n);
	int x = 360;
	for (uint8_t i = 0; i < n; ++i) {
		if (f[i].state != SIM_DIV_DEAD && f[i].x > x) x = f[i].x;
	}
	return x;
}

static void paint_field(const front_art_t *a) {
	const uint32_t step = ui_anim_step();
	static uint32_t last_step;
	const bool tick = step != last_step && !battle_holds();
	last_step = step;
	const uint8_t god = battle_god();

	alert_take(god);
	int target = front_mid() - 128;
	target = target < 0 ? 0 : target > 256 ? 256 : target;
	cam_x += (target - cam_x) / 8;
	render_camera(cam_x, CAM_Y);
	const int cy = CAM_Y;

	field_map = a->map;
	paint_side(SIM_SIDE_OURS, RENDER_CIV_HELLENES, step, cy);
	paint_side(SIM_SIDE_FOE, foe_civ(a), step, cy);
	/* the generals watch from behind their lines                      */
	const int ap = (alert_frames > 0) ? ALERT_FRAMES - alert_frames : -1;
	const bool commanding = ap >= 0 && (alert_event == SIM_EVT_RALLY ||
	                                    alert_event == SIM_EVT_CHARGE);
	const int ox = lane_clamp(SIM_LANE_NORTH, 56, 12);
	render_squad(RENDER_CIV_HELLENES, RENDER_CLS_GENERAL, 0u,
	             commanding ? RENDER_ANIM_COMMAND : 2u, step,
	             ox - 8 - cam_x, lane_y(SIM_LANE_NORTH, ox) - 10 - cy, 0u);
	if (god != RENDER_GOD_NONE) {
		const int gx = foe_rear() + 40;
		god_tick(god, gx, tick);
		render_god(god, (uint8_t)god_anim, (uint32_t)god_step,
		           gx - cam_x, LANE_Y[SIM_LANE_NORTH] - 60 - cy, RENDER_SPR_FLIP);
		if (fx_frame >= 0) {
			render_fx(god, (uint8_t)fx_frame, fx_x - cam_x, fx_y - cy,
			          RENDER_SPR_FLIP);
			if (tick && ++fx_frame >= 6) {
				fx_frame = -1;
			}
		}
	} else {
		const int fx = lane_clamp(SIM_LANE_NORTH, foe_rear() + 40, 12);
		render_squad(foe_civ(a), RENDER_CLS_GENERAL, 0u, 2u, step,
		             fx - 8 - cam_x, lane_y(SIM_LANE_NORTH, fx) - 10 - cy,
		             RENDER_SPR_FLIP);
	}
	paint_fallen(tick, cy);
	if (flash > 0) {
		render_flash((uint8_t)(flash > 6 ? 12 : flash * 2));
		flash--;
	} else {
		render_flash(0u);
	}

	/* top plate: the front, the clock, the speed actually running      */
	char buf[48], t0[8], t1[8];
	const sim_province_record_t *tab = sim_province_table(NULL);
	render_rect(RENDER_TOP, 0, 0, 256, 13, RENDER_INK);
	clock_text(t0, sizeof t0, sim_battle_time());
	clock_text(t1, sizeof t1, sim_battle_time_cap());
	snprintf(buf, sizeof buf, "%s  %s/%s  %s", tab[sim_battle_province()].name, t0, t1,
	         battle_holds() ? "||" : sim_speed_badge());
	render_text(RENDER_TOP, 4, 3, RENDER_BRONZE_L, buf);
	if (god != RENDER_GOD_NONE) {
		render_text(RENDER_TOP, 196, 3, RENDER_CRIT_L, god_name(god));
		if (sim_battle_boss_phase() >= 2u) {
			render_text(RENDER_TOP, 240, 3, RENDER_CRIT_L, "II");
		}
	} else if (sim_battle_kind() != SIM_WAR_CONQUEST) {
		render_text(RENDER_TOP, 196, 3, RENDER_WARN_L, sim_war_name(sim_battle_kind()));
	}
	/* the alert plate: 2 s under the header, colour + a lead glyph so
	 * the severity never rides on colour alone (constitution 7)        */
	if (alert_frames > 0) {
		const char *copy = sim_battle_event_copy();
		if (copy != NULL && alert_event == sim_battle_event()) {
			const uint8_t col = alert_color(alert_event);
			render_rect(RENDER_TOP, 0, 14, 256, 13, RENDER_INK);
			render_text(RENDER_TOP, 4, 17, col,
			            col == RENDER_CRIT_L ? "!!" : col == RENDER_WARN_L ? "! " : "> ");
			render_text(RENDER_TOP, 18, 17, col, copy);
		}
		if (!battle_holds()) {
			alert_frames--;
		}
	} else if (sim_battle_time() < 8u * SIM_BATTLE_HZ) {
		/* the first seconds teach the two controls that matter       */
		render_rect(RENDER_TOP, 0, 14, 256, 13, RENDER_INK);
		render_text(RENDER_TOP, 4, 17, RENDER_ACCENT_L, "> ");
		render_text(RENDER_TOP, 18, 17, RENDER_ACCENT_L, str(STR_TIP_BATTLE));
	}
	if (paused) {
		render_rect(RENDER_TOP, 88, 88, 80, 16, RENDER_INK);
		render_text(RENDER_TOP, 96, 92, RENDER_BRONZE_L, "PAUSED (A)");
	}
}

/* ---- art: the command panel -------------------------------------------- */
static void minimap(void) {
	/* 512 -> 124 px: x * 31 / 128                                        */
	const int ox = 5, oy = 27;
	for (uint8_t side = 0; side < 2u; ++side) {
		uint8_t n = 0u;
		const sim_division_t *d = sim_battle_divisions(side, &n);
		for (uint8_t i = 0; i < n; ++i) {
			if (d[i].state == SIM_DIV_DEAD) {
				continue;
			}
			/* a dot per division: square = ours, bar = theirs, so the
			 * sides read without colour (constitution 7)            */
			render_rect(RENDER_BOTTOM, ox + d[i].x * 31 / 128,
			            oy + LANE_Y[d[i].lane % SIM_LANES] * 31 / 128,
			            side ? 4 : 3, side ? 2 : 3,
			            side ? RENDER_CRIT_L : RENDER_ACCENT_L);
		}
	}
	render_box(RENDER_BOTTOM, ox + cam_x * 31 / 128, oy + CAM_Y * 31 / 128,
	           62, 46, RENDER_BRONZE_L);
}

static void paint_panel(void) {
	/* B22 HP tap-expand: the reading panel over the minimap; the sim is
	 * held while it is up (ui_sim_hold), so reading costs 0 ticks.      */
	char buf[48];
	render_panel(RENDER_BOTTOM, 3, 25, 128, 128, RENDER_PANEL_SHEET);
	render_text(RENDER_BOTTOM, 10, 32, RENDER_INK, "ORDER PANEL");
	snprintf(buf, sizeof buf, "Ours %u of %u", (unsigned)sim_battle_units(),
	         (unsigned)sim_battle_units_start());
	render_text(RENDER_BOTTOM, 10, 46, RENDER_INK, buf);
	render_bar(RENDER_BOTTOM, 10, 56, 114, sim_battle_units(),
	           sim_battle_units_start(), RENDER_BAR_HP);
	snprintf(buf, sizeof buf, "Foe %u of %u", (unsigned)sim_battle_enemies(),
	         (unsigned)sim_battle_enemies_start());
	render_text(RENDER_BOTTOM, 10, 68, RENDER_INK, buf);
	render_bar(RENDER_BOTTOM, 10, 78, 114, sim_battle_enemies(),
	           sim_battle_enemies_start(), RENDER_BAR_BOSS);
	snprintf(buf, sizeof buf, "%s, %s.",
	         sim_formation_name(sim_formation_kind()),
	         sim_terrain_name(sim_front_terrain(sim_battle_province())));
	render_text(RENDER_BOTTOM, 10, 92, RENDER_INK2, buf);
	render_text_wrap(RENDER_BOTTOM, 10, 104, 114, 3, RENDER_INK,
	                 battle_state_copy());
	render_text(RENDER_BOTTOM, 10, 136, RENDER_ACCENT, "Held. X closes.");
}

/* GAME_DESIGN 7: morale both sides + the flanks, under the minimap.
 * Every state is a word as well as a colour (grayscale-safe).          */
static void paint_status(uint8_t god) {
	static const char *const FLANK[3] = {"secure", "pressed", "BROKEN"};
	static const uint8_t FLANK_COL[3] = {RENDER_LINE, RENDER_WARN_L, RENDER_CRIT_L};
	char buf[32];
	render_panel(RENDER_BOTTOM, 3, 113, 128, 41, RENDER_PANEL_IRON);
	render_icon(RENDER_BOTTOM, RENDER_ICON_MORALE, 6, 115, RENDER_SPR_FRONT);
	snprintf(buf, sizeof buf, "%s %u", sim_battle_morale() < 30u ? "Shaken" : "Morale",
	         (unsigned)sim_battle_morale());
	render_text(RENDER_BOTTOM, 24, 117, sim_battle_morale() < 30u ? RENDER_WARN_L
	                                                             : RENDER_SURFACE, buf);
	render_bar(RENDER_BOTTOM, 76, 118, 50, sim_battle_morale(), SIM_MORALE_MAX,
	           RENDER_BAR_MORALE);
	if (god != RENDER_GOD_NONE) {
		/* the god's countdown: the omen is a plan, not a surprise   */
		const uint8_t eta = sim_battle_boss_eta();
		if (sim_battle_boss_phase() >= 2u && eta == 0u) {
			snprintf(buf, sizeof buf, "%s ENRAGED", god_name(god));
		} else {
			snprintf(buf, sizeof buf, "%s strikes %us", god_name(god), (unsigned)eta);
		}
		render_text(RENDER_BOTTOM, 8, 130, eta <= 6u ? RENDER_CRIT_L : RENDER_WARN_L, buf);
	} else {
		snprintf(buf, sizeof buf, "%s %u", sim_battle_foe_morale() < 20u ? "Foe flees" : "Foe",
		         (unsigned)sim_battle_foe_morale());
		render_text(RENDER_BOTTOM, 8, 130, RENDER_SURFACE, buf);
		render_bar(RENDER_BOTTOM, 76, 131, 50, sim_battle_foe_morale(),
		           SIM_MORALE_MAX, RENDER_BAR_BOSS);
	}
	const uint8_t fl = sim_battle_flank() > 2u ? 2u : sim_battle_flank();
	snprintf(buf, sizeof buf, "Flanks %s", FLANK[fl]);
	render_text(RENDER_BOTTOM, 8, 143, FLANK_COL[fl], buf);
}

/* the general's two abilities: name + key, or the seconds left         */
static void paint_ability(int x, int w, uint8_t ab, const char *name,
                          const char *key) {
	char buf[24];
	const uint8_t cd = sim_ability_cooldown(ab);
	if (cd > 0u) {
		snprintf(buf, sizeof buf, "%s %us", name, (unsigned)cd);
	} else {
		snprintf(buf, sizeof buf, "%s %s", name, key);
	}
	const bool armed = ab == SIM_ABILITY_CHARGE && sim_charge_armed();
	render_button(RENDER_BOTTOM, x, 140, w, 18, buf,
	              panel_open || cd > 0u ? RENDER_BTN_DISABLED
	              : armed ? RENDER_BTN_FOCUS : RENDER_BTN_NORMAL);
}

static void paint_bottom(const front_art_t *a) {
	char buf[40], t0[8];
	const sim_province_record_t *tab = sim_province_table(NULL);
	const uint8_t god = battle_god();
	render_backdrop(RENDER_BOTTOM, a->panel, god);
	render_text(RENDER_BOTTOM, 8, 8, RENDER_BRONZE_L, tab[sim_battle_province()].name);
	clock_text(t0, sizeof t0, sim_battle_time());
	render_text(RENDER_BOTTOM, 96, 8, RENDER_LINE, t0);
	/* the fallen, counted where the eye goes after the field (GDD 7)   */
	{
		const unsigned lost = sim_battle_units_start() > sim_battle_units()
		                          ? (unsigned)(sim_battle_units_start() - sim_battle_units())
		                          : 0u;
		render_icon(RENDER_BOTTOM, RENDER_ICON_CASUALTIES, 128, 3, RENDER_SPR_FRONT);
		snprintf(buf, sizeof buf, "%s %u", str(STR_FALLEN), lost);
		render_text(RENDER_BOTTOM, 146, 8, lost ? RENDER_CRIT_L : RENDER_LINE, buf);
	}
	const uint8_t sp = sim_speed();
	render_icon(RENDER_BOTTOM, battle_holds() ? RENDER_ICON_PAUSE
	                           : sp == SIM_SPEED_X4 ? RENDER_ICON_SPEED4
	                           : sp == SIM_SPEED_X2 ? RENDER_ICON_SPEED2
	                                                : RENDER_ICON_SPEED1,
	            232, 3, RENDER_SPR_FRONT);
	render_text(RENDER_BOTTOM, 208, 8, RENDER_SURFACE, sim_speed_badge());

	if (panel_open) {
		paint_panel();
	} else {
		minimap();
		paint_status(god);
	}
	/* 2x2 stance tiles, d-pad == taps; the stance in force is focused    */
	static const char *const LABEL[4] = {"Advance", "Flank", "Hold", "Fall back"};
	static const char *const KEY[4] = {"Up", "Right", "Left", "Down"};
	for (int t = 0; t < 4; ++t) {
		const int x = 134 + (t & 1) * 60, y = 26 + (t >> 1) * 34;
		const bool on = sim_battle_stance() == TILE_ORDER[t];
		render_button(RENDER_BOTTOM, x, y, 58, 30, LABEL[t],
		              panel_open ? RENDER_BTN_DISABLED
		              : on ? RENDER_BTN_FOCUS : RENDER_BTN_NORMAL);
		render_text(RENDER_BOTTOM, x + 4, y + 21, RENDER_INK2, KEY[t]);
		if (on && !panel_open) {
			render_ring(RENDER_BOTTOM, x, y, 58, 30);
		}
	}
	render_button(RENDER_BOTTOM, 134, 94, 118, 22,
	              paused ? "Paused: A resumes" : "Pause (A)",
	              paused ? RENDER_BTN_FOCUS : RENDER_BTN_NORMAL);
	render_button(RENDER_BOTTOM, 134, 120, 58, 18, "Undo (B)",
	              sim_undo_armed() ? RENDER_BTN_NORMAL : RENDER_BTN_DISABLED);
	snprintf(buf, sizeof buf, "Speed %s", sim_speed_badge());
	render_button(RENDER_BOTTOM, 196, 120, 56, 18, buf, RENDER_BTN_NORMAL);
	paint_ability(134, 58, SIM_ABILITY_RALLY, "Rally", "Y");
	paint_ability(196, 56, SIM_ABILITY_CHARGE, "Charge", "L");

	/* the alert plate: pin shape per state + the live copy (rows 12/14) */
	static const uint8_t PIN[5] = {0u, 1u, 3u, 0u, 2u};
	render_pin(RENDER_BOTTOM, PIN[battle_state_id()], 6, 166, RENDER_SPR_FRONT);
}

static const ui_row_t BATTLE_ROWS[] = {
	{14, RENDER_BOTTOM, 26, 165, 226, 1, RENDER_SURFACE},
	{12, RENDER_BOTTOM, 26, 176, 226, 1, RENDER_LINE},
	{16, RENDER_BOTTOM, 26, 176, 226, 1, RENDER_LINE},
};

static void paint_intro(void) {
	const uint8_t god = battle_god();
	render_backdrop(RENDER_TOP, RENDER_BD_STAGE, god);
	render_backdrop(RENDER_BOTTOM, RENDER_BD_CARD, god);
	render_rect(RENDER_TOP, 0, 180, 256, 12, RENDER_INK);
	render_text(RENDER_TOP, 4, 182, RENDER_BRONZE_L, "A: take the field");
	{
		/* GAME_DESIGN 8: time to understand the threat - the god's
		 * omen and its answer, and what defeating it is worth        */
		const uint8_t boss = sim_battle_boss();
		char buf[48];
		render_rect(RENDER_BOTTOM, 0, 160, 256, 32, RENDER_INK);
		snprintf(buf, sizeof buf, "Omen, then answer: %s", sim_boss_omen(boss));
		render_text(RENDER_BOTTOM, 4, 163, RENDER_WARN_L, buf);
		snprintf(buf, sizeof buf, "Relic: %s. %s", sim_relic_name(boss),
		         sim_relic_brief(boss));
		render_text(RENDER_BOTTOM, 4, 177, RENDER_BRONZE_L, buf);
	}
	if (god == RENDER_GOD_ZEUS && intro_frames == INTRO_FRAMES - 30) {
		flash = 10;
	}
	if (flash > 0) {
		render_flash((uint8_t)(flash > 6 ? 12 : flash * 2));
		flash--;
	} else {
		render_flash(0u);
	}
	if (--intro_frames == 0) {
		render_flash(0u);
	}
}

void battle_frame(void) {
	char line[80], t0[8], t1[8];
	ui_draw_layout(BATTLE_ROWS, (uint8_t)(sizeof BATTLE_ROWS / sizeof BATTLE_ROWS[0]));
	if (!sim_battle_active() && !sim_battle_sealed()) {
		ui_draw_text(0, 0, "FRONT: no battle running");
		ui_draw_text(0, 14, battle_state_copy());
		return;
	}
	const front_art_t *a = front_art(sim_battle_province());
	if (intro_frames > 0) {
		paint_intro();
		ui_draw_text(0, 0, "BOSS INTRO");
		return;
	}
	render_backdrop(RENDER_TOP, a->map, battle_god());
	paint_field(a);
	paint_bottom(a);

	const sim_snapshot_t *snap = sim_snapshot();
	const sim_province_record_t *tab = sim_province_table(NULL);
	static const char *const STANCE[4] = {"ADV", "HOLD", "FLK", "FAL"};

	clock_text(t0, sizeof t0, sim_battle_time());
	clock_text(t1, sizeof t1, sim_battle_time_cap());
	snprintf(line, sizeof line, "%s/%s  FRONT %s  %s", t0, t1,
	         tab[sim_battle_province()].name, sim_war_name(sim_battle_kind()));
	ui_draw_text(0, 0, line);
	/* GAME_DESIGN 7 readouts on the semantic channel (row 17; 18..23
	 * are the dev overlay's): morale, flank, the general, the alert     */
	{
		const char *ev = sim_battle_event_copy();
		snprintf(line, sizeof line, "M%u F%u FL%u R%u C%u %s",
		         (unsigned)sim_battle_morale(), (unsigned)sim_battle_foe_morale(),
		         (unsigned)sim_battle_flank(),
		         (unsigned)sim_ability_cooldown(SIM_ABILITY_RALLY),
		         (unsigned)sim_ability_cooldown(SIM_ABILITY_CHARGE),
		         ev != NULL ? ev : "");
		ui_draw_text(0, 17, line);
	}

	/* the 1-row strip (<= 32 px; spec-04 R1): mm | clock | ETC | HP     */
	snprintf(line, sizeof line, "|mm|%s ETC %u|HP %u/%u|%s|%s|%s|",
	         t0, (unsigned)sim_battle_enemies(),
	         (unsigned)sim_battle_units(),
	         (unsigned)sim_battle_units_start(),
	         STANCE[sim_battle_stance() & 3u],
	         sim_undo_armed() ? "UNDO" : "UNDO off", /* T023 chip       */
	         paused ? "||" : sim_speed_badge()); /* R4: badge == rate    */
	ui_draw_text(0, 11, line);
	/* 2x2 stance tiles (the touch hit map, T020), D-pad == taps.        */
	ui_draw_text(0, 9, "[ADV] [FLK]");
	ui_draw_text(0, 10, "[HOLD][FAL]");
	ui_draw_text(0, 13, "D-pad = stance, A = pause, B = undo, R = speed");
	/* R5: while a clamp is live the strip says what actually runs -
	 * byte-exact per spec-04 states ("Clamp: \"x4 -> x2: 4.9 ms..."). */
	{
		const char *cc = sim_speed_clamp_copy();
		ui_draw_text(0, 12, cc != NULL ? cc : "");
	}
	/* ack / refusal copy (sim-owned, 2 s hold); with nothing to ack
	 * the plate carries the written state (T030).                      */
	{
		const char *st = sim_order_status();
		ui_draw_text(0, 14, st != NULL ? st : battle_state_copy());
	}
	if (panel_open) {
		ui_draw_text(0, 16, "Panel open: battle held. X closes.");
	} else if (paused) {
		ui_draw_text(0, 16, "Paused: battle held. A resumes.");
	} else if (sim_speed_clamp_copy() == NULL) {
		ui_draw_text(0, 16, "Up/Left/Right/Down set the stance.");
	}
	snprintf(line, sizeof line, "TREASURY %lu",
	         (unsigned long)snap->treasury);
	ui_draw_text(0, 15, line);
}

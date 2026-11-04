/* battle.c - top battlefield + bottom command panel (B10, spec-04 R1-R12).
 * B20 speed, B21 orders + one-tap undo, B22 the order panel (HP expand
 * with tick + step hold, 0 catch-up), the five written states and the
 * art pass: the high camera over the front's 512x512 field, both lines
 * as squads (12 men per sprite) thinning with the sim's counts, the god
 * of a divine front with its attack effect, the live minimap, and the
 * boss intro (stage on top, card below, <= 2 s, once per deploy).
 * The copy rows stay the semantic channel (host tests); the sealed frame
 * is never redrawn (R11) - ui.c switches to Result in <= 1 frame.
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

/* B21: the last touch target ("ring armed" pattern for the dangerous
 * End-turn bar; orders/undo/speed are direct taps) + the conduit seq
 * stamp for order/commit/undo commands.                                  */
static int last_touch = -1;
static uint32_t order_seq_stamp;

/* B22: the reading surfaces. Both hold the tick and the battle step.    */
static bool panel_open;
static int intro_frames;           /* > 0 while the boss intro plays     */
#define INTRO_FRAMES 120           /* 2 s at 60 Hz (B30: <= 2 s)          */

/* the view's memory of the sim counts, to animate the deltas            */
static uint8_t seen_units, seen_enemies, seen_turn;
static int god_anim, god_step, god_timer;
static int fx_frame = -1, fx_x, fx_y;
static int flash;
static int line_x = 256;           /* map x of the front line            */
static int cam_x = 128;

/* GAME_DESIGN 7 contextual alerts: each new sim event shows once on the
 * top plate for 2 s with the alert cue; the field acts it out.          */
#define ALERT_FRAMES 120
static uint16_t seen_event_seq;
static int alert_frames;
static uint8_t alert_event;

bool battle_holds(void) {
	return panel_open || intro_frames > 0;
}

void battle_enter(bool intro) {
	const front_art_t *a = front_art(sim_battle_province());
	intro_frames = (intro && a->god != RENDER_GOD_NONE) ? INTRO_FRAMES : 0;
	panel_open = false;
	last_touch = -1;
	seen_units = sim_battle_units();
	seen_enemies = sim_battle_enemies();
	seen_turn = 0u;
	god_anim = RENDER_GOD_WALK;
	god_step = god_timer = 0;
	fx_frame = -1;
	flash = 0;
	line_x = 256;
	cam_x = line_x - 128;
	seen_event_seq = sim_battle_event_seq();
	alert_frames = 0;
	alert_event = SIM_EVT_NONE;
}

/* ---- the five written states (spec-04 T030) ----------------------------- */
enum { BST_EMPTY = 0, BST_LOADING, BST_ERROR, BST_SUCCESS, BST_PARTIAL };

uint8_t battle_state_id(void);
const char *battle_state_copy(void);

uint8_t battle_state_id(void) {
	if (!sim_battle_active() && !sim_battle_sealed()) {
		return BST_EMPTY;
	}
	if (sim_battle_turn() == 0u && sim_orders_pending() == 0u) {
		return BST_LOADING;
	}
	const char *st = sim_order_status();
	if (st != NULL && (strstr(st, "not queued") || strstr(st, "need"))) {
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
		snprintf(copy, sizeof copy, "Deploying %u companies. Turn 1 next.",
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

/* B21 (T020): the bottom hit map on the command panel art:
 * header speed chip (py < 22, px >= 184), minimap = order panel toggle
 * (px < 132), 2x2 order tiles at x 134/194, y 26/60 (58 x 30), End-turn
 * bar y 94..117 (TWO-TAP commit), Undo chip y 120..139 x < 194, speed
 * chip x >= 194, the general's Rally / Charge y 140..159 (8 / 9).
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
		return 4; /* End-turn bar: two-tap commit                    */
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

/* tile index (touch order) -> sim order                                  */
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
	/* B21 touch direct-select: orders/undo/speed are one tap; the
	 * dangerous End-turn commit needs two taps on the same target.      */
	if (kind == (uint8_t)UI_EVT_TOUCH) {
		const int t = touch_target(value);
		if (t < 0) {
			return;
		}
		if (t == 7) {
			panel_open = !panel_open; /* B22: HP tap-expand         */
			last_touch = -1;
			return;
		}
		if (panel_open) {
			return; /* the panel reads; orders wait for it to close */
		}
		if (t <= 3) {
			send_cmd(7u, TILE_ORDER[t]);
			last_touch = -1;
			return;
		}
		if (t == 5) {
			send_cmd(9u, 0u);
			last_touch = -1;
			return;
		}
		if (t == 6) {
			cycle_speed();
			last_touch = -1;
			return;
		}
		if (t == 8 || t == 9) {
			send_cmd(10u, t == 8 ? SIM_ABILITY_RALLY : SIM_ABILITY_CHARGE);
			last_touch = -1;
			return;
		}
		/* t == 4: two-tap End turn (undo closes at commit)          */
		if (t == last_touch) {
			last_touch = -1;
			send_cmd(8u, 0u);
		} else {
			last_touch = 4;
		}
		return;
	}
	if (kind != (uint8_t)UI_EVT_KEY) {
		return;
	}
	/* B21 (T020/T023): D-pad raises an order, A commits (End turn),
	 * B undoes the last order (single-level), R cycles speed (B20),
	 * X opens/closes the order panel (B22, the HP handle), Y rallies and
 * L charges (the general, GAME_DESIGN 7). All four
	 * order paths reuse the same handler for full d-pad/touch parity.
	 * Orders land only in a live battle; the sim acks (<= 16.7 ms
	 * inline) and the strip copies.                                     */
	last_touch = -1;
	const uint32_t keys = value;
	if (keys & KEY_X) {
		panel_open = !panel_open;
		return;
	}
	if (panel_open) {
		if (keys & KEY_B) {
			panel_open = false; /* B backs out of the reading panel */
		}
		return;
	}
	uint8_t order;
	if (keys & KEY_UP) {
		order = SIM_ORDER_ADVANCE;
	} else if (keys & KEY_LEFT) {
		order = SIM_ORDER_HOLD;
	} else if (keys & KEY_RIGHT) {
		order = SIM_ORDER_FLANK;
	} else if (keys & KEY_DOWN) {
		order = SIM_ORDER_FALLBACK;
	} else if (keys & KEY_A) {
		/* End turn = the T024 commit: flush, close undo, 2 s copy.  */
		send_cmd(8u, 0u);
		return;
	} else if (keys & KEY_B) {
		send_cmd(9u, 0u);
		return;
	} else if (keys & KEY_R) {
		cycle_speed();
		return;
	} else if (keys & (KEY_Y | KEY_L)) {
		send_cmd(10u, (keys & KEY_Y) ? SIM_ABILITY_RALLY : SIM_ABILITY_CHARGE);
		return;
	} else {
		return;
	}
	send_cmd(7u, order);
}

/* ---- art: the field ------------------------------------------------------- */
#define FIELD_Y 200  /* map y of the line's top rank                      */
#define CAM_Y 150    /* the high camera's fixed tilt over the field      */
#define MAX_OURS 32  /* 8 ranks x 4 files of squads                      */
#define MAX_FOES 24

static int squad_x_player(int i) { return line_x - 40 - (i % 4) * 34 - (i / 4 % 2) * 8; }
static int squad_y(int i) { return FIELD_Y + (i / 4) * 16; }

static void god_tick(const front_art_t *a, bool advance) {
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
			fx_x = line_x - 110;
			fx_y = FIELD_Y + 10;
			if (a->god == RENDER_GOD_ZEUS) {
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
		return RENDER_WARN_L;
	case SIM_EVT_FLANK_BROKEN:
	case SIM_EVT_CAV_CHARGE:
	case SIM_EVT_BOSS_STRIKE:
	case SIM_EVT_BOSS_PHASE:
	case SIM_EVT_COLLAPSE:
		return RENDER_CRIT_L;
	default:
		return RENDER_ACCENT_L;
	}
}

/* a new sim event: cue it, and let the field act it out                */
static void alert_take(const front_art_t *a) {
	if (sim_battle_event_seq() == seen_event_seq) {
		return;
	}
	seen_event_seq = sim_battle_event_seq();
	alert_event = sim_battle_event();
	alert_frames = ALERT_FRAMES;
	audio_cue(alert_color(alert_event) == RENDER_ACCENT_L ? AUDIO_CUE_CONFIRM
	                                                    : AUDIO_CUE_ALERT);
	if (a->god != RENDER_GOD_NONE &&
	    (alert_event == SIM_EVT_BOSS_STRIKE || alert_event == SIM_EVT_BOSS_OMEN ||
	     alert_event == SIM_EVT_BOSS_PHASE)) {
		god_anim = RENDER_GOD_SPECIAL; /* the god casts it            */
		god_step = god_timer = 0;
	}
	if (a->god == RENDER_GOD_ZEUS && alert_event == SIM_EVT_BOSS_STRIKE) {
		flash = 12; /* the thunderbolt lands                            */
	}
}

static void paint_field(const front_art_t *a) {
	const uint32_t step = ui_anim_step();
	static uint32_t last_step;
	const bool tick = step != last_step;
	last_step = step;

	/* sim deltas drive the drama: a loss = the enemy strikes, a kill =
	 * the god (or the enemy line) staggers                              */
	const uint8_t units = sim_battle_units();
	const uint8_t enemies = sim_battle_enemies();
	if (a->god != RENDER_GOD_NONE) {
		if (units < seen_units && god_anim != RENDER_GOD_ATTACK) {
			god_anim = RENDER_GOD_ATTACK;
			god_step = god_timer = 0;
		} else if (enemies < seen_enemies && god_anim == RENDER_GOD_IDLE) {
			god_anim = RENDER_GOD_HURT;
			god_step = god_timer = 0;
		}
	}
	if (sim_battle_turn() != seen_turn) {
		/* the next queued order sets where the line goes            */
		seen_turn = sim_battle_turn();
		line_x += 4;
	}
	const uint8_t next = sim_orders_pending() ? sim_order_at(0u) : 0xFFu;
	if (tick) {
		if (next == SIM_ORDER_ADVANCE && line_x < 300) line_x++;
		if (next == SIM_ORDER_FALLBACK && line_x > 200) line_x--;
	}
	seen_units = units;
	seen_enemies = enemies;
	alert_take(a);
	god_tick(a, tick);
	/* frames into the running alert: the field's acting clock         */
	const int ap = (alert_frames > 0) ? ALERT_FRAMES - alert_frames : -1;
	const bool raid = ap >= 0 && (alert_event == SIM_EVT_CAV_CHARGE ||
	                              alert_event == SIM_EVT_FLANK_BROKEN ||
	                              alert_event == SIM_EVT_FLANK_HELD);
	const bool rout = sim_battle_foe_morale() < 20u && a->god == RENDER_GOD_NONE;
	const bool wavering = sim_battle_morale() < 30u;

	cam_x += ((line_x - 128) - cam_x) / 8;
	render_camera(cam_x, CAM_Y);
	const int cy = CAM_Y;

	/* player line: one squad per company, 4 per rank, cap 40 sprites  */
	const int shown = units > MAX_OURS ? MAX_OURS : (int)units;
	const bool engaged = sim_battle_turn() > 0u;
	for (int i = 0; i < shown; ++i) {
		uint8_t cls = (i % 4 == 3) ? RENDER_CLS_ARCHER : RENDER_CLS_SPEAR;
		if (i >= MAX_OURS - 4) {
			cls = RENDER_CLS_CAVALRY;
		}
		int x = squad_x_player(i);
		if (cls == RENDER_CLS_CAVALRY && next == SIM_ORDER_FLANK) {
			x += 40; /* the riders swing wide on a flank             */
		}
		if (cls == RENDER_CLS_CAVALRY && sim_charge_armed()) {
			x += 56; /* the general's charge: riders at the front    */
		}
		const uint8_t strength = (i == shown - 1 && units < sim_battle_units_start()) ? 2u : 0u;
		/* shaken ranks flinch: every third squad plays its hurt cycle */
		const uint8_t anim = (wavering && i % 3 == 1) ? RENDER_ANIM_HURT
		                     : (next == SIM_ORDER_HOLD) ? RENDER_ANIM_IDLE
		                     : engaged ? RENDER_ANIM_ATTACK : RENDER_ANIM_WALK;
		render_squad(RENDER_CIV_HELLENES, cls, strength, anim,
		             step + (uint32_t)i, x - cam_x, squad_y(i) - cy, 0u);
	}
	if (ap >= 0 && alert_event == SIM_EVT_COLLAPSE && shown < MAX_OURS) {
		/* the company that broke: it falls where it stood          */
		render_squad(RENDER_CIV_HELLENES, RENDER_CLS_SPEAR, 3u,
		             RENDER_ANIM_DEATH, (uint32_t)(ap / 8),
		             squad_x_player(shown) - cam_x, squad_y(shown) - cy, 0u);
	}
	const bool commanding = ap >= 0 && (alert_event == SIM_EVT_RALLY ||
	                                    alert_event == SIM_EVT_CHARGE);
	render_squad(RENDER_CIV_HELLENES, RENDER_CLS_GENERAL, 0u,
	             commanding ? RENDER_ANIM_COMMAND : 2u, step,
	             line_x - 200 - cam_x, FIELD_Y - 30 - cy, 0u);
	if (raid) {
		/* the enemy riders sweep round the open wing (or break on the
		 * ring and wheel away when the Circle holds)                   */
		const int reach = (alert_event == SIM_EVT_FLANK_HELD)
		                      ? (ap < 50 ? ap * 2 : 100 - (ap - 50))
		                      : ap * 2;
		for (int r = 0; r < 2; ++r) {
			render_squad(a->enemy, RENDER_CLS_CAVALRY, 0u, RENDER_ANIM_WALK,
			             step + (uint32_t)r, line_x + 40 - reach - r * 22 - cam_x,
			             FIELD_Y + 132 + r * 10 - cy, RENDER_SPR_FLIP);
		}
	}

	/* enemy line: one squad per enemy, the front one thinning          */
	const int foes = (a->god != RENDER_GOD_NONE) ? (enemies > 1u ? enemies - 1 : 0)
	                                             : enemies;
	for (int i = 0; i < foes && i < MAX_FOES; ++i) {
		const int x = line_x + 8 + (i % 4) * 34 + (i / 4 % 2) * 8 +
		              (a->god != RENDER_GOD_NONE ? 60 : 0);
		const uint8_t strength = (i == foes - 1) ? (uint8_t)(3u - (enemies % 3u)) % 4u : 0u;
		/* a god's minions form up below its feet, not over it    */
		const int y = squad_y(i) + (a->god != RENDER_GOD_NONE ? 80 : 0);
		/* a broken enemy turns its back and drifts off the field    */
		render_squad(a->enemy, (i % 4 == 3) ? RENDER_CLS_ARCHER : RENDER_CLS_SPEAR,
		             strength, rout ? RENDER_ANIM_WALK
		                       : engaged ? RENDER_ANIM_ATTACK : RENDER_ANIM_WALK,
		             step + (uint32_t)i * 3u,
		             x + (rout ? 12 + (int)(step % 8u) : 0) - cam_x, y - cy,
		             rout ? 0u : RENDER_SPR_FLIP);
	}
	if (a->god != RENDER_GOD_NONE) {
		render_god(a->god, (uint8_t)god_anim, (uint32_t)god_step,
		           line_x - 10 - cam_x, FIELD_Y - 40 - cy, RENDER_SPR_FLIP);
		if (fx_frame >= 0) {
			render_fx(a->god, (uint8_t)fx_frame, fx_x - cam_x, fx_y - cy,
			          RENDER_SPR_FLIP);
			if (tick && ++fx_frame >= 6) {
				fx_frame = -1;
			}
		}
	} else {
		render_squad(a->enemy, RENDER_CLS_GENERAL, 0u, 2u, step,
		             line_x + 170 - cam_x, FIELD_Y - 30 - cy, RENDER_SPR_FLIP);
	}
	if (flash > 0) {
		render_flash((uint8_t)(flash > 6 ? 12 : flash * 2));
		flash--;
	} else {
		render_flash(0u);
	}

	/* top plate: the front, the turn, the speed actually running       */
	char buf[48];
	const sim_province_record_t *tab = sim_province_table(NULL);
	render_rect(RENDER_TOP, 0, 0, 256, 13, RENDER_INK);
	snprintf(buf, sizeof buf, "%s  Turn %u/%u  %s", tab[sim_battle_province()].name,
	         (unsigned)sim_battle_turn(), (unsigned)sim_battle_turn_cap(),
	         sim_speed_badge());
	render_text(RENDER_TOP, 4, 3, RENDER_BRONZE_L, buf);
	if (a->god != RENDER_GOD_NONE) {
		render_text(RENDER_TOP, 196, 3, RENDER_CRIT_L, god_name(a->god));
		if (sim_battle_boss_phase() >= 2u) {
			render_text(RENDER_TOP, 232, 3, RENDER_CRIT_L, "II");
		}
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
		alert_frames--;
	}
}

/* ---- art: the command panel -------------------------------------------- */
static void minimap(const front_art_t *a) {
	/* 512 -> 124 px: x * 31 / 128                                        */
	const int ox = 5, oy = 27;
	const int units = sim_battle_units() > MAX_OURS ? MAX_OURS : (int)sim_battle_units();
	for (int i = 0; i < units; i += 2) {
		render_rect(RENDER_BOTTOM, ox + squad_x_player(i) * 31 / 128,
		            oy + squad_y(i) * 31 / 128, 3, 2, RENDER_ACCENT_L);
	}
	for (int i = 0; i < sim_battle_enemies() && i < MAX_FOES; i += 2) {
		const int x = line_x + 8 + (i % 4) * 34 + (a->god != RENDER_GOD_NONE ? 60 : 0);
		render_rect(RENDER_BOTTOM, ox + x * 31 / 128,
		            oy + squad_y(i) * 31 / 128, 3, 2, RENDER_CRIT_L);
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
	snprintf(buf, sizeof buf, "%s, %s. Queue %u/32",
	         sim_formation_name(sim_formation_kind()),
	         sim_terrain_name(sim_front_terrain(sim_battle_province())),
	         (unsigned)sim_orders_pending());
	render_text(RENDER_BOTTOM, 10, 92, RENDER_INK2, buf);
	render_text_wrap(RENDER_BOTTOM, 10, 104, 114, 3, RENDER_INK,
	                 battle_state_copy());
	render_text(RENDER_BOTTOM, 10, 136, RENDER_ACCENT, "Held. X closes.");
}

/* GAME_DESIGN 7: morale both sides + the flanks, under the minimap.
 * Every state is a word as well as a colour (grayscale-safe).          */
static void paint_status(const front_art_t *a) {
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
	if (a->god != RENDER_GOD_NONE) {
		snprintf(buf, sizeof buf, "%s %s", god_name(a->god),
		         sim_battle_boss_phase() >= 2u ? "ENRAGED" : "unbowed");
		render_text(RENDER_BOTTOM, 8, 130, RENDER_CRIT_L, buf);
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

/* the general's two abilities: name + key, or the turns left           */
static void paint_ability(int x, int w, uint8_t ab, const char *name,
                          const char *key) {
	char buf[24];
	const uint8_t cd = sim_ability_cooldown(ab);
	if (cd > 0u) {
		snprintf(buf, sizeof buf, "%s %u", name, (unsigned)cd);
	} else {
		snprintf(buf, sizeof buf, "%s %s", name, key);
	}
	const bool armed = ab == SIM_ABILITY_CHARGE && sim_charge_armed();
	render_button(RENDER_BOTTOM, x, 140, w, 18, buf,
	              panel_open || cd > 0u ? RENDER_BTN_DISABLED
	              : armed ? RENDER_BTN_FOCUS : RENDER_BTN_NORMAL);
}

static void paint_bottom(const front_art_t *a) {
	char buf[40];
	const sim_province_record_t *tab = sim_province_table(NULL);
	render_backdrop(RENDER_BOTTOM, a->panel, a->god);
	render_text(RENDER_BOTTOM, 8, 8, RENDER_BRONZE_L, tab[sim_battle_province()].name);
	snprintf(buf, sizeof buf, "Turn %u/%u", (unsigned)sim_battle_turn(),
	         (unsigned)sim_battle_turn_cap());
	render_text(RENDER_BOTTOM, 96, 8, RENDER_LINE, buf);
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
		minimap(a);
		paint_status(a);
	}
	/* 2x2 order tiles, d-pad == taps                                     */
	static const char *const LABEL[4] = {"Advance", "Flank", "Hold", "Fall back"};
	static const char *const KEY[4] = {"Up", "Right", "Left", "Down"};
	const uint8_t next = sim_orders_pending() ? sim_order_at(0u) : 0xFFu;
	for (int t = 0; t < 4; ++t) {
		const int x = 134 + (t & 1) * 60, y = 26 + (t >> 1) * 34;
		render_button(RENDER_BOTTOM, x, y, 58, 30, LABEL[t],
		              panel_open ? RENDER_BTN_DISABLED
		              : next == TILE_ORDER[t] ? RENDER_BTN_FOCUS
		                                      : RENDER_BTN_NORMAL);
		render_text(RENDER_BOTTOM, x + 4, y + 21, RENDER_INK2, KEY[t]);
	}
	render_button(RENDER_BOTTOM, 134, 94, 118, 22,
	              last_touch == 4 ? "Tap again: End turn" : "End turn (A)",
	              last_touch == 4 ? RENDER_BTN_FOCUS : RENDER_BTN_NORMAL);
	if (last_touch == 4) {
		render_ring(RENDER_BOTTOM, 134, 94, 118, 22);
	}
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

static void paint_intro(const front_art_t *a) {
	render_backdrop(RENDER_TOP, RENDER_BD_STAGE, a->god);
	render_backdrop(RENDER_BOTTOM, RENDER_BD_CARD, a->god);
	render_rect(RENDER_TOP, 0, 180, 256, 12, RENDER_INK);
	render_text(RENDER_TOP, 4, 182, RENDER_BRONZE_L, "A: take the field");
	{
		/* GAME_DESIGN 8: time to understand the threat - the god's
		 * omen and its answer, and what defeating it is worth        */
		const uint8_t boss = sim_front_boss(sim_battle_province());
		char buf[48];
		render_rect(RENDER_BOTTOM, 0, 160, 256, 32, RENDER_INK);
		snprintf(buf, sizeof buf, "Omen, then answer: %s", sim_boss_omen(boss));
		render_text(RENDER_BOTTOM, 4, 163, RENDER_WARN_L, buf);
		snprintf(buf, sizeof buf, "Relic: %s. %s", sim_relic_name(boss),
		         sim_relic_brief(boss));
		render_text(RENDER_BOTTOM, 4, 177, RENDER_BRONZE_L, buf);
	}
	if (a->god == RENDER_GOD_ZEUS && intro_frames == INTRO_FRAMES - 30) {
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
	char line[80];
	ui_draw_layout(BATTLE_ROWS, (uint8_t)(sizeof BATTLE_ROWS / sizeof BATTLE_ROWS[0]));
	if (!sim_battle_active() && !sim_battle_sealed()) {
		ui_draw_text(0, 0, "FRONT: no battle running");
		ui_draw_text(0, 14, battle_state_copy());
		return;
	}
	const front_art_t *a = front_art(sim_battle_province());
	if (intro_frames > 0) {
		paint_intro(a);
		ui_draw_text(0, 0, "BOSS INTRO");
		return;
	}
	render_backdrop(RENDER_TOP, a->map, a->god);
	paint_field(a);
	paint_bottom(a);

	const sim_snapshot_t *snap = sim_snapshot();
	const sim_province_record_t *tab = sim_province_table(NULL);

	snprintf(line, sizeof line, "T%u/%u  FRONT %s  ETC T%u",
	         (unsigned)sim_battle_turn(), (unsigned)sim_battle_turn_cap(),
	         tab[sim_battle_province()].name,
	         (unsigned)(sim_battle_turn_cap() - sim_battle_turn()));
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

	/* the 1-row strip (<= 32 px; spec-04 R1): mm | turn | ETC | HP       */
	snprintf(line, sizeof line, "|mm|T%u ETC %u|HP %u/%u|%s|%s|q %u/32|",
	         (unsigned)sim_battle_turn(),
	         (unsigned)sim_battle_enemies(),
	         (unsigned)sim_battle_units(),
	         (unsigned)sim_battle_units_start(),
	         sim_undo_armed() ? "UNDO" : "UNDO off", /* T023 chip       */
	         sim_speed_badge(), /* R4: badge text == active rate          */
	         (unsigned)queue_diag()->cmd_depth);
	ui_draw_text(0, 11, line);
	/* 2x2 order tiles (the touch hit map, T020), D-pad == taps.         */
	ui_draw_text(0, 9, "[ADV] [FLK]");
	ui_draw_text(0, 10, "[HOLD][FAL]");
	ui_draw_text(0, 13, "D-pad = order, A = End turn, B = Undo, R = speed");
	/* R5: while a clamp is live the strip says what actually runs -
	 * byte-exact per spec-04 states ("Clamp: \"x4 -> x2: 4.9 ms..."). */
	{
		const char *cc = sim_speed_clamp_copy();
		ui_draw_text(0, 12, cc != NULL ? cc : "");
	}
	/* ack / reject / commit copy (sim-owned, incl. the 2 s hold); with
	 * nothing to ack the plate carries the written state (T030).       */
	{
		const char *st = sim_order_status();
		ui_draw_text(0, 14, st != NULL ? st : battle_state_copy());
	}
	if (panel_open) {
		ui_draw_text(0, 16, "Panel open: battle held. X closes.");
	} else if (sim_speed_clamp_copy() == NULL) {
		ui_draw_text(0, 16, "Up/Down/Left/Right order. A end turn.");
	}
	snprintf(line, sizeof line, "TREASURY %lu",
	         (unsigned long)snap->treasury);
	ui_draw_text(0, 15, line);
}

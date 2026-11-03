/* battle.c - top battlefield + bottom 1-row strip (B10, spec-04 R1-R3).
 * Walking skeleton: auto-resolve only - the order tiles, speed badge and
 * Undo chip land with B20/B21/B22; the strip carries the read fields that
 * exist at B10 (front, turn, chips, HP). The field is text chips until
 * the B27/B31 pixel pass; the sealed frame is never redrawn (R11).
 */
#include <nds.h>
#include <stdio.h>
#include <string.h>

#include "contracts/sim_api.h"
#include "contracts/queue.h"
#include "contracts/audio.h"
#include "contracts/ui.h"

#include "ui_draw.h"
#include "screens.h"

/* B21: the last touch target ("ring armed" pattern for the dangerous
 * End-turn bar; orders/undo/speed are direct taps) + the conduit seq
 * stamp for order/commit/undo commands.                                  */
static int last_touch = -1;
static uint32_t order_seq_stamp;

/* B21 (T020): the bottom hit map, spec-04 B1 geometry schema in text
 * layout: 2x2 order tiles (py 24..135), End-turn bar (py 136..151,
 * TWO-TAP commit), strip row (py >= 152: undo chip px < 208, speed
 * chip px >= 208). -1 = outside (tap ignored, input preserved).         */
static int touch_target(uint32_t value) {
	const uint32_t px = (value >> 8) & 0xFFu;
	const uint32_t py = value & 0xFFu;
	if (py < 24u) {
		return -1;
	}
	if (py < 136u) {
		const uint32_t col = (px < 128u) ? 0u : 1u;
		const uint32_t row = (py < 80u) ? 0u : 1u;
		return (int)(row * 2u + col); /* ADV FLK / HOL FAL */
	}
	if (py < 152u) {
		return 4; /* End-turn bar: two-tap commit                    */
	}
	if (px < 208u) {
		return 5; /* undo chip                                         */
	}
	return 6; /* speed chip                                            */
}

static void send_cmd(uint8_t code, uint8_t arg) {
	sim_cmd_t cmd;
	memset(&cmd, 0, sizeof cmd);
	cmd.seq = ++order_seq_stamp;
	cmd.cmd = code;
	cmd.arg = arg;
	sim_enqueue(&cmd);
}

void battle_input(uint8_t kind, uint32_t value) {
	/* B21 touch direct-select: orders/undo/speed are one tap; the
	 * dangerous End-turn commit needs two taps on the same target.      */
	if (kind == (uint8_t)UI_EVT_TOUCH) {
		const int t = touch_target(value);
		if (t < 0) {
			return;
		}
		if (t <= 3) {
			send_cmd(7u, (uint8_t)t);
			return;
		}
		if (t == 5) {
			send_cmd(9u, 0u);
			return;
		}
		if (t == 6) {
			uint8_t next = sim_speed_request() * 2u;
			if (next > SIM_SPEED_X4) {
				next = SIM_SPEED_X1;
			}
			sim_set_speed(next);
			audio_cue(AUDIO_CUE_SPEED);
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
	 * B undoes the last order (single-level), R cycles speed (B20).
	 * All four order paths reuse the same handler for full
	 * d-pad/touch parity. Orders land only in a live battle; the
	 * sim acks (<= 16.7 ms inline) and the strip copies.            */
	last_touch = -1;
	const uint32_t keys = value;
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
		uint8_t next = sim_speed_request() * 2u;
		if (next > SIM_SPEED_X4) {
			next = SIM_SPEED_X1;
		}
		sim_set_speed(next);
		audio_cue(AUDIO_CUE_SPEED); /* id only, drop if unavailable  */
		return;
	} else {
		return;
	}
	send_cmd(7u, order);
}

void battle_frame(void) {
	char line[80];
	if (!sim_battle_active() && !sim_battle_sealed()) {
		ui_draw_text(0, 0, "FRONT: no battle running");
		return;
	}
	const sim_snapshot_t *snap = sim_snapshot();
	const sim_province_record_t *tab = sim_province_table(NULL);

	snprintf(line, sizeof line, "T%u/9  FRONT %s  ETC T%u",
	         (unsigned)sim_battle_turn(), tab[sim_battle_province()].name,
	         (unsigned)(SIM_BATTLE_TURNS - sim_battle_turn()));
	ui_draw_text(0, 0, line);

	/* field: 6 chip rows stand in for the 64 OAM units (text fidelity)   */
	const uint8_t chips = sim_battle_units();
	uint8_t drawn = 0u;
	for (int r = 0; r < 6; ++r) {
		size_t used = 0u;
		line[0] = '\0';
		for (int c = 0; c < 10 && used < sizeof line - 4u; ++c) {
			if (drawn < chips) {
				drawn++;
				used += (size_t)snprintf(line + used,
				                         sizeof line - used, "[=]");
			} else {
				used += (size_t)snprintf(line + used,
				                         sizeof line - used, "...");
			}
		}
		ui_draw_text(0, 2 + r, line);
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
	/* R5: while a clamp is live the strip says what actually runs -
	 * byte-exact per spec-04 states ("Clamp: \"x4 -> x2: 4.9 ms..."). */
	ui_draw_text(0, 13, "D-pad = order, A = End turn, B = Undo, R = speed");
	{
		const char *cc = sim_speed_clamp_copy();
		if (cc != NULL) {
			ui_draw_text(0, 12, cc);
		} else {
			ui_draw_text(0, 12, "");
		}
	}
	/* ack / reject / commit copy (sim-owned, incl. the 2 s hold).       */
	{
		const char *st = sim_order_status();
		ui_draw_text(0, 14, st != NULL ? st : "");
	}
	snprintf(line, sizeof line, "TREASURY %lu",
	         (unsigned long)snap->treasury);
	ui_draw_text(0, 15, line);
}

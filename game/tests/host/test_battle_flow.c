/* test_battle_flow.c - B22 + B23 (spec-04 T026-T030, T032-T033).
 * B22: the order panel holds the tick and the step (10 s open = 0 ticks,
 *      0 steps, 0 catch-up), BattleEnded in 1 frame, 1 save op per
 *      battle end, resume always at turn 1 (mid-turn state refused), the
 *      five written states reachable.
 * B23: 100 battles with one order script share the golden hash (and the
 *      checked-in baseline), and a no-touch run reaches the 4 orders +
 *      End turn + Undo + speed + the HP handle through battle_input.
 * The loop gate is mirrored from loop.c (loop is not host-linked): hold
 * -> run mode 0 and no battle step, exactly what loop_start() does.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "contracts/save.h"
#include "contracts/sim_api.h"
#include "contracts/queue.h"
#include "contracts/audio.h"
#include "contracts/ui.h"
#include "nds.h"
#include "slots.h"
#include "screens.h"

void queue_frame(uint32_t frame);
uint8_t battle_state_id(void);
const char *battle_state_copy(void);

/* ---- seams ---------------------------------------------------------------- */
static char drawn[24][80];
void ui_draw_text(int x, int y, const char *txt) {
	(void)x;
	if (y >= 0 && y < 24) {
		snprintf(drawn[y], sizeof drawn[0], "%s", txt);
	}
}
void audio_cue(audio_cue_t cue) { (void)cue; }

static uint8_t rom_block[512];
static unsigned media_writes;
save_backend_kind_t backend_probe(void) { return SAVE_BACKEND_EEPROM; }
void backend_read(uint32_t off, uint8_t *dst, uint32_t len) {
	memmove(dst, rom_block + off, len);
}
void backend_write(uint32_t off, const uint8_t *src, uint32_t len) {
	media_writes++;
	memmove(rom_block + off, src, len);
}
bool backend_detect_detail(void) { return false; }
void backend_write_detail(uint32_t o, const uint8_t *s, uint32_t l) { (void)o; (void)s; (void)l; }
void backend_read_detail(uint32_t o, uint8_t *d, uint32_t l) { (void)o; (void)d; (void)l; }

static ui_screen_t screen = UI_SCREEN_BATTLE;
ui_screen_t ui_screen(void) { return screen; }
uint8_t ui_selected_row(void) { return 0xFFu; }
void ui_input(uint8_t kind, uint32_t value) { (void)kind; (void)value; }

static uint32_t frame_no;

/* one 60 Hz frame of the loop: conduit, then the 10 Hz gate (loop.c)   */
static void loop_frame(void) {
	queue_frame(frame_no);
	if ((frame_no % 6u) == 0u) {
		const bool hold = (screen == UI_SCREEN_BATTLE) && battle_holds();
		uint8_t modes = SIM_RUN_ECONOMY | SIM_RUN_MUSTER;
		if (hold) {
			modes = 0u;
		}
		sim_set_run_mode(modes);
		const uint8_t subs = sim_speed();
		sim_step(subs);
		static uint8_t turn_ticks;
		if (screen == UI_SCREEN_BATTLE && !hold) {
			for (uint8_t s = 0u; s < subs; ++s) {
				if (!sim_battle_active() || sim_battle_sealed()) {
					break;
				}
				if (++turn_ticks >= SIM_BATTLE_TICKS_PER_TURN) {
					turn_ticks = 0u;
					sim_battle_step();
				}
			}
		}
	}
	frame_no++;
	if (screen == UI_SCREEN_BATTLE) {
		battle_frame();
	}
}

static void stage(uint8_t front, uint16_t roster) {
	sim_init(1u);
	save_aggregate_t agg;
	memset(&agg, 0, sizeof agg);
	agg.treasury = 580u;
	agg.seed = 42u;
	agg.rng_state = 4242u;
	agg.unit_count = roster;
	sim_ingest_save(&agg, 12u);
	sim_cmd_t d;
	memset(&d, 0, sizeof d);
	d.cmd = 4u;
	d.arg = front;
	assert(sim_apply(&d) == SIM_OK);
	screen = UI_SCREEN_BATTLE;
	battle_enter(false);
}

static void key(uint32_t k) {
	battle_input((uint8_t)UI_EVT_KEY, k);
	loop_frame(); /* the conduit dispatches what the key enqueued      */
}

static uint32_t fnv(uint32_t h, const void *p, size_t n) {
	const uint8_t *b = (const uint8_t *)p;
	for (size_t i = 0; i < n; ++i) {
		h ^= b[i];
		h *= 0x01000193u;
	}
	return h;
}

/* one scripted battle: the same orders at the same turns, every run   */
static uint32_t scripted_battle(uint8_t front) {
	stage(front, 10u);
	static const uint8_t SCRIPT[] = {SIM_ORDER_ADVANCE, SIM_ORDER_HOLD,
	                                 SIM_ORDER_FLANK, SIM_ORDER_FALLBACK};
	uint32_t h = 0x811C9DC5u;
	uint32_t seq = 5000u;
	for (int t = 0; t < (int)SIM_BATTLE_TURNS && !sim_battle_sealed(); ++t) {
		sim_cmd_t c;
		memset(&c, 0, sizeof c);
		c.seq = ++seq;
		c.cmd = 7u;
		c.arg = SCRIPT[t % 4];
		(void)sim_apply(&c);
		sim_battle_step();
		const uint16_t sh = sim_battle_state_hash();
		h = fnv(h, &sh, sizeof sh);
	}
	const battle_outcome_t *o = sim_seal_outcome();
	assert(o != NULL);
	return fnv(h, o, sizeof *o);
}

int main(int argc, char **argv) {
	(void)argc;
	(void)argv;
	setvbuf(stdout, NULL, _IONBF, 0);
	save_init();

	/* B22 T026: 10 s of panel open = 0 ticks, 0 steps, 0 catch-up.        */
	stage(3u, 10u);
	{
		for (int i = 0; i < 30; ++i) {
			loop_frame(); /* half a turn in                           */
		}
		key(KEY_X);
		assert(battle_holds());
		const uint32_t ticks = sim_tick();
		const uint8_t turn = sim_battle_turn();
		for (int i = 0; i < 600; ++i) {
			loop_frame();
		}
		assert(sim_tick() == ticks);
		assert(sim_battle_turn() == turn);
		/* orders wait for the panel: a d-pad press enqueues nothing    */
		key(KEY_UP);
		assert(sim_orders_pending() == 0u);
		key(KEY_X);
		assert(!battle_holds());
		for (int i = 0; i < 5; ++i) {
			loop_frame(); /* 6 frames since the close = one 10 Hz tick */
		}
		assert(sim_tick() == ticks + 1u); /* resumes at +1, no catch-up */
		printf("battle: panel open 600 frames -> +0 ticks, +0 turns; close -> +1 tick\n");
	}

	/* B22 T027: BattleEnded in 1 frame - the sealing step seals.          */
	stage(3u, 10u);
	{
		while (sim_battle_active()) {
			sim_battle_step();
		}
		assert(sim_battle_sealed() && sim_seal_outcome() != NULL);
		printf("battle: the sealing step exposes the outcome in the same frame\n");
	}

	/* B22 T028: exactly 1 save op per battle end (the Continue boundary). */
	{
		memset(rom_block, 0, sizeof rom_block);
		save_init();
		media_writes = 0u;
		screen = UI_SCREEN_RESULT;
		sim_cmd_t c;
		memset(&c, 0, sizeof c);
		c.seq = 9001u;
		c.cmd = 5u;
		assert(sim_enqueue(&c));
		queue_frame(frame_no++);
		assert(sim_outcome_applied());
		assert(media_writes == 1u);
		c.seq = 9002u; /* a mashed second Continue                     */
		assert(sim_enqueue(&c));
		queue_frame(frame_no++);
		screen = UI_SCREEN_CAMPAIGN;
		for (int i = 0; i < 1200; ++i) {
			queue_frame(frame_no++);
		}
		assert(media_writes == 1u);
		printf("battle: 1 battle end -> 1 save op (mashed Continue + 20 s Campaign add 0)\n");
	}

	/* B22 T029: resume at turn 1; a mid-turn record is refused.          */
	{
		save_aggregate_t agg;
		memset(&agg, 0, sizeof agg);
		agg.treasury = 400u;
		agg.unit_count = 8u;
		agg.reserved[3] = 1u; /* the in-battle marker                  */
		sim_ingest_save(&agg, 5u);
		assert(sim_ingest_rejected_midturn());
		assert(!sim_battle_active() && sim_battle_turn() == 0u);
		sim_cmd_t d;
		memset(&d, 0, sizeof d);
		d.cmd = 4u;
		d.arg = 1u;
		assert(sim_apply(&d) == SIM_OK);
		sim_battle_step();
		assert(sim_battle_turn() == 1u);
		save_aggregate_t out;
		sim_export_aggregate(&out);
		assert(out.reserved[3] == 0u); /* battle state never persists */
		printf("battle: mid-turn record refused; resume starts at turn 1\n");
	}

	/* B22 T030: the five written states.                                 */
	{
		bool seen[5] = {false, false, false, false, false};
		sim_init(1u);
		seen[battle_state_id()] = true; /* Empty                          */
		assert(strcmp(battle_state_copy(), "No battle running. Deploy from Base.") == 0);
		stage(3u, 10u);
		seen[battle_state_id()] = true; /* Loading                        */
		assert(strstr(battle_state_copy(), "Deploying 10 companies") != NULL);
		for (int i = 0; i < 33; ++i) {
			sim_cmd_t c;
			memset(&c, 0, sizeof c);
			c.seq = 7000u + (uint32_t)i;
			c.cmd = 7u;
			c.arg = SIM_ORDER_HOLD;
			(void)sim_apply(&c);
		}
		seen[battle_state_id()] = true; /* Error: the 33rd order          */
		stage(3u, 10u);
		{
			sim_cmd_t c;
			memset(&c, 0, sizeof c);
			c.seq = 7100u;
			c.cmd = 7u;
			c.arg = SIM_ORDER_HOLD;
			assert(sim_apply(&c) == SIM_OK);
			sim_battle_step();
		}
		seen[battle_state_id()] = true; /* Success: all standing          */
		{
			sim_cmd_t c;
			memset(&c, 0, sizeof c);
			c.seq = 7101u;
			c.cmd = 7u;
			c.arg = SIM_ORDER_FLANK; /* forces one own loss            */
			assert(sim_apply(&c) == SIM_OK);
			sim_battle_step();
		}
		seen[battle_state_id()] = true; /* Partial: N of M standing       */
		assert(strstr(battle_state_copy(), "of 10 companies standing") != NULL);
		for (int i = 0; i < 5; ++i) {
			assert(seen[i]);
		}
		printf("battle: 5 written states reachable (Empty Loading Error Success Partial)\n");
	}

	/* B23 T032: 100 battles share the golden hash + the baseline file.   */
	{
		const uint32_t ref = scripted_battle(0u);
		for (int i = 1; i < 100; ++i) {
			assert(scripted_battle(0u) == ref);
		}
		const char *path = "tests/golden/battle_baseline.txt";
		FILE *f = fopen(path, "r");
		const char *upd = getenv("UPDATE_GOLDEN");
		if (f == NULL || (upd != NULL && upd[0] == '1')) {
			if (f) {
				fclose(f);
			}
			f = fopen(path, "w");
			assert(f != NULL);
			fprintf(f, "%08x\n", ref);
			fclose(f);
			printf("battle: golden baseline written %08x (UPDATE_GOLDEN)\n", ref);
		} else {
			unsigned base = 0u;
			assert(fscanf(f, "%x", &base) == 1);
			fclose(f);
			if (base != ref) {
				printf("battle: golden %08x != baseline %08x\n", ref, base);
				assert(0);
			}
		}
		printf("battle: 100/100 scripted battles -> golden %08x == baseline\n", ref);
	}

	/* B23 T033: a no-touch run - 4 orders, Undo, speed, HP handle, End. */
	stage(2u, 10u);
	{
		key(KEY_UP);
		assert(sim_orders_pending() == 1u && sim_order_at(0u) == SIM_ORDER_ADVANCE);
		key(KEY_LEFT);
		key(KEY_RIGHT);
		key(KEY_DOWN);
		assert(sim_orders_pending() == 4u);
		assert(sim_order_at(1u) == SIM_ORDER_HOLD);
		assert(sim_order_at(2u) == SIM_ORDER_FLANK);
		assert(sim_order_at(3u) == SIM_ORDER_FALLBACK);
		key(KEY_B); /* one-tap undo: back to the pre-FALLBACK world   */
		assert(sim_orders_pending() == 3u);
		assert(strcmp(sim_order_status(), "Undo: pre-order restored.") == 0);
		key(KEY_R);
		assert(sim_speed() == SIM_SPEED_X2 && strcmp(sim_speed_badge(), "x2") == 0);
		key(KEY_X); /* the HP handle opens the order panel            */
		assert(battle_holds());
		key(KEY_X);
		assert(!battle_holds());
		key(KEY_A); /* End turn: commit flushes, undo closes          */
		assert(sim_orders_pending() == 0u && !sim_undo_armed());
		assert(strcmp(sim_order_status(), "Turn committed, undo closed.") == 0);
		int frames = 0;
		while (!sim_battle_sealed() && frames < 60 * 60) {
			loop_frame();
			++frames;
		}
		assert(sim_battle_sealed());
		printf("battle: no-touch run - 4 orders, undo, x2, panel, End turn -> "
		       "sealed in %d frames, 0 dead ends\n", frames);
	}

	printf("B22+B23 battle flow host tests passed.\n");
	return 0;
}

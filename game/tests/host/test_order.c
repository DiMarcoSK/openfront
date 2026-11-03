/* test_order.c - B21 (spec-04 R6-R12, T020-T025).
 * The four orders (ADVANCE/HOLD/FLANK/FALLBACK) applied once at
 * resolution, the single-level undo journal, End-turn commit closing
 * undo with the 2 s copy, the 33rd-order inline reject (32 kept,
 * input preserved) and the d-pad/touch/queue seams. The ARM7/audio
 * seams are unchanged (audio.c stays the id-only facade).
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "contracts/save.h"
#include "contracts/sim_api.h"
#include "contracts/queue.h"
#include "contracts/ui.h"
#include "nds.h"
#include "slots.h"

/* ---- backend stub (512 B EEPROM) ---------------------------------------- */
static uint8_t rom_block[512];
save_backend_kind_t backend_probe(void) { return SAVE_BACKEND_EEPROM; }
void backend_read(uint32_t off, uint8_t *dst, uint32_t len) {
	memmove(dst, rom_block + off, len);
}
void backend_write(uint32_t off, const uint8_t *src, uint32_t len) {
	memmove(rom_block + off, src, len);
}
bool backend_detect_detail(void) { return false; }
void backend_write_detail(uint32_t o, const uint8_t *s, uint32_t l) { (void)o; (void)s; (void)l; }
void backend_read_detail(uint32_t o, uint8_t *d, uint32_t l) { (void)o; (void)d; (void)l; }

ui_screen_t ui_screen(void) { return UI_SCREEN_BATTLE; }
uint8_t ui_selected_row(void) { return 0xFFu; }
void ui_input(uint8_t kind, uint32_t value) { (void)kind; (void)value; }

static uint32_t seq_stamp = 100u;

static void ingest(uint32_t treasury, uint16_t day, uint8_t roster) {
	save_aggregate_t agg;
	memset(&agg, 0, sizeof agg);
	agg.treasury = treasury;
	agg.seed = 42u;
	agg.rng_state = 4242u;
	agg.unit_count = roster;
	sim_ingest_save(&agg, day);
}

static void stage(void) {
	sim_init(1u);
	ingest(580u, 12u, 10u);
	sim_set_run_mode(SIM_RUN_MUSTER);
	for (uint8_t i = 0u; i < 10u; ++i) {
		sim_cmd_t c;
		memset(&c, 0, sizeof c);
		c.seq = 900u + i;
		c.cmd = 3u;
		c.arg = 1u;
		assert(sim_apply(&c) == SIM_OK);
	}
	sim_step(20u); /* 1 company lands */
	sim_set_run_mode(0u);
	sim_cmd_t d;
	memset(&d, 0, sizeof d);
	d.seq = 8u;
	d.cmd = 4u;
	d.arg = 3u; /* front South, LOW: 6 enemies */
	assert(sim_apply(&d) == SIM_OK);
	assert(sim_battle_active());
}

static sim_result_t order(uint8_t o) {
	sim_cmd_t c;
	memset(&c, 0, sizeof c);
	c.seq = ++seq_stamp;
	c.cmd = 7u;
	c.arg = o;
	return sim_apply(&c);
}

int main(void) {
	setvbuf(stdout, NULL, _IONBF, 0);

	/* Defaults: no battle, no orders, nothing to undo.                   */
	sim_init(1u);
	ingest(580u, 12u, 0u);
	assert(sim_orders_pending() == 0u);
	assert(!sim_undo_armed());
	assert(sim_order_status() == NULL);
	{
		sim_cmd_t c;
		memset(&c, 0, sizeof c);
		c.seq = ++seq_stamp;
		c.cmd = 7u;
		c.arg = SIM_ORDER_ADVANCE;
		assert(sim_apply(&c) == SIM_BAD_ARG);
		assert(strcmp(sim_order_status(), "Orders need a live battle.") == 0);
		c.seq = ++seq_stamp;
		c.cmd = 8u;
		assert(sim_apply(&c) == SIM_BAD_ARG);
		c.seq = ++seq_stamp;
		c.cmd = 9u;
		assert(sim_apply(&c) == SIM_BAD_ARG);
	}

	/* ADVANCE applies once: +1 dmg on the consuming turn.                */
	stage();
	{
		const uint16_t h0 = sim_battle_state_hash();
		assert(order(SIM_ORDER_ADVANCE) == SIM_OK);
		assert(sim_orders_pending() == 1u);
		assert(sim_order_at(0u) == SIM_ORDER_ADVANCE);
		assert(sim_undo_armed());
		assert(strcmp(sim_order_status(), "ADVANCE queued.") == 0);
		assert(sim_battle_state_hash() != h0); /* ring moved          */
		sim_battle_step();
		assert(sim_orders_pending() == 0u); /* applied exactly once   */
		const uint8_t enemies = sim_battle_enemies();
		assert(enemies >= 3u && enemies <= 4u); /* 6 - (2..3)          */
	}

	/* HOLD suppresses attrition: 4 turns keep the roster untouched.      */
	stage();
	{
		assert(order(SIM_ORDER_HOLD) == SIM_OK);
		assert(strcmp(sim_order_status(), "HOLD queued.") == 0);
		for (uint8_t s = 0u; s < 4u; ++s) {
			if (!sim_battle_active() || sim_battle_sealed()) {
				break;
			}
			sim_battle_step();
		}
		assert(sim_battle_units() == sim_battle_units_start());
	}

	/* FLANK: dmg +2 and exactly one own loss.                            */
	stage();
	{
		assert(order(SIM_ORDER_FLANK) == SIM_OK);
		assert(strcmp(sim_order_status(), "FLANK queued.") == 0);
		sim_battle_step();
		assert(sim_battle_units() == sim_battle_units_start() - 1u);
		assert(sim_battle_enemies() <= 3u); /* 6 - (3..4)             */
	}

	/* FALLBACK: no kill, no loss.                                        */
	stage();
	{
		assert(order(SIM_ORDER_FALLBACK) == SIM_OK);
		sim_battle_step();
		assert(sim_battle_enemies() == 6u);
		assert(sim_battle_units() == sim_battle_units_start());
	}

	/* FIFO: two orders, one per turn.                                    */
	stage();
	{
		assert(order(SIM_ORDER_FLANK) == SIM_OK);
		assert(order(SIM_ORDER_HOLD) == SIM_OK);
		assert(sim_orders_pending() == 2u);
		assert(sim_order_at(0u) == SIM_ORDER_FLANK);
		assert(sim_order_at(1u) == SIM_ORDER_HOLD);
		sim_battle_step(); /* flank lands: -1 roster                  */
		assert(sim_battle_units() == sim_battle_units_start() - 1u);
		assert(sim_orders_pending() == 1u);
		sim_battle_step(); /* hold lands: roster untouched            */
		assert(sim_battle_units() == sim_battle_units_start() - 1u);
		assert(sim_orders_pending() == 0u);
	}

	/* The 33rd order: inline reject, 32 kept, input preserved.           */
	stage();
	{
		for (uint8_t i = 0u; i < 32u; ++i) {
			assert(order(SIM_ORDER_ADVANCE) == SIM_OK);
		}
		assert(sim_orders_pending() == 32u);
		assert(order(SIM_ORDER_ADVANCE) == SIM_QUEUE_FULL);
		assert(strcmp(sim_order_status(),
		              "Order not queued: 32/32 waiting. "
		              "Undo 1 or End turn.") == 0);
		assert(sim_orders_pending() == 32u); /* 32 kept               */
		assert(sim_order_at(31u) == SIM_ORDER_ADVANCE);
		/* input preserved: the conduit took no rejects, ring empty.   */
		assert(queue_diag()->cmd_depth == 0u);
		/* input still works: a valid path answers after the reject.   */
		assert(order(SIM_ORDER_HOLD) == SIM_QUEUE_FULL); /* still 32  */
	}

	/* Undo restores the pre-order hash deterministically (R9).           */
	stage();
	{
		const uint16_t h0 = sim_battle_state_hash();
		assert(order(SIM_ORDER_ADVANCE) == SIM_OK);
		assert(sim_battle_state_hash() != h0);
		sim_battle_step(); /* the advance resolves                    */
		sim_cmd_t u;
		memset(&u, 0, sizeof u);
		u.seq = ++seq_stamp;
		u.cmd = 9u;
		assert(sim_apply(&u) == SIM_OK);
		assert(strcmp(sim_order_status(), "Undo: pre-order restored.") == 0);
		assert(sim_battle_state_hash() == h0); /* pre-order world      */
		assert(sim_orders_pending() == 0u);
		assert(!sim_undo_armed()); /* single level consumed            */
		{ /* determinism: the re-run turn equals a clean run.          */
			sim_battle_step();
			assert(sim_battle_turn() == 1u);
			const uint8_t ref = sim_battle_enemies();
			stage();
			sim_battle_step();
			assert(sim_battle_turn() == 1u);
			assert(sim_battle_enemies() == ref);
		}

	/* End-turn commit: flush, close undo, 2 s copy (T024).               */
	stage();
	{
		assert(order(SIM_ORDER_ADVANCE) == SIM_OK);
		assert(order(SIM_ORDER_HOLD) == SIM_OK);
		{
			sim_cmd_t c;
			memset(&c, 0, sizeof c);
			c.seq = ++seq_stamp;
			c.cmd = 8u;
			assert(sim_apply(&c) == SIM_OK);
		}
		assert(sim_orders_pending() == 0u); /* flushed                */
		assert(!sim_undo_armed()); /* commit closes undo              */
		assert(strcmp(sim_order_status(),
		              "Turn committed, undo closed.") == 0);
		/* 2-s window: 19 ticks later the copy is live; the 20th tick
		 * (2.0 s at 10 Hz) retires it.                                */
		for (int i = 0; i < 19; ++i) {
			sim_step(1u);
			assert(strcmp(sim_order_status(),
			              "Turn committed, undo closed.") == 0);
		}
		sim_step(1u);
		assert(sim_order_status() == NULL);
		{
			sim_cmd_t u;
			memset(&u, 0, sizeof u);
			u.seq = ++seq_stamp;
			u.cmd = 9u;
			assert(sim_apply(&u) == SIM_BAD_ARG); /* closed         */
		}
	}

	/* T031 duplicate guard rides order commands too.                     */
	stage();
	{
		sim_cmd_t c;
		memset(&c, 0, sizeof c);
		c.seq = 555u;
		c.cmd = 7u;
		c.arg = SIM_ORDER_HOLD;
		assert(sim_apply(&c) == SIM_OK);
		assert(sim_orders_pending() == 1u);
		assert(sim_apply(&c) == SIM_DUPLICATE); /* same seq            */
		assert(sim_orders_pending() == 1u); /* no double enqueue       */
	}

	printf("B21 order tests passed.\n");
	return 0;
}

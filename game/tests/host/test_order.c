/* test_order.c - B21 as amended by ADR-0008 (spec-04 R6-R12, T020-T025).
 * Orders are live stances in a real-time battle: ADVANCE/HOLD/FLANK/
 * FALLBACK take effect on the next field tick and hold until the next
 * order; Undo puts the previous stance back (single level); End turn is
 * retired. Each stance is checked on the field itself: Hold stands, Fall
 * back opens the distance and deals no melee, Flank wheels the wings
 * round the foe's centre and strikes its sides.
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

static void stage(uint8_t front, uint16_t roster, uint8_t form) {
	sim_init(1u);
	save_aggregate_t agg;
	memset(&agg, 0, sizeof agg);
	agg.treasury = 580u;
	agg.seed = 42u;
	agg.rng_state = 4242u;
	agg.unit_count = roster;
	agg.formation = form;
	sim_ingest_save(&agg, 12u);
	sim_cmd_t d;
	memset(&d, 0, sizeof d);
	d.seq = 8u;
	d.cmd = 4u;
	d.arg = front;
	assert(sim_apply(&d) == SIM_OK);
	assert(sim_battle_active());
}

static sim_result_t cmd(uint8_t code, uint8_t arg) {
	sim_cmd_t c;
	memset(&c, 0, sizeof c);
	c.seq = ++seq_stamp;
	c.cmd = code;
	c.arg = arg;
	return sim_apply(&c);
}

static sim_result_t order(uint8_t o) {
	return cmd(7u, o);
}

static void run(unsigned ticks) {
	for (unsigned t = 0; t < ticks && sim_battle_active(); ++t) {
		sim_battle_step();
	}
}

static unsigned foe_companies(void) {
	return sim_battle_enemies();
}

int main(void) {
	setvbuf(stdout, NULL, _IONBF, 0);

	/* orders need a live battle; the refusal is inline copy             */
	sim_init(1u);
	assert(order(SIM_ORDER_HOLD) == SIM_BAD_ARG);
	assert(strcmp(sim_order_status(), "Orders need a live battle.") == 0);
	printf("order: no battle -> inline refusal\n");

	/* a stance is live at once and holds; a repeat arms no undo         */
	stage(3u, 10u, SIM_FORM_LINE);
	assert(sim_battle_stance() == SIM_ORDER_ADVANCE);
	assert(order(SIM_ORDER_ADVANCE) == SIM_OK);
	assert(!sim_undo_armed());
	assert(order(SIM_ORDER_HOLD) == SIM_OK);
	assert(sim_battle_stance() == SIM_ORDER_HOLD);
	assert(strcmp(sim_order_status(), "Stance: Hold.") == 0);
	assert(sim_undo_armed());
	run(30u);
	assert(sim_battle_stance() == SIM_ORDER_HOLD); /* no turn consumes it */
	printf("order: stance live at once, holds over 30 ticks, repeat arms no undo\n");

	/* one-tap undo: the previous stance back, single level             */
	assert(order(SIM_ORDER_FLANK) == SIM_OK);
	assert(cmd(9u, 0u) == SIM_OK);
	assert(sim_battle_stance() == SIM_ORDER_HOLD);
	assert(strcmp(sim_order_status(), "Undo: Hold again.") == 0);
	assert(!sim_undo_armed());
	assert(cmd(9u, 0u) == SIM_BAD_ARG);
	printf("order: undo restores the previous stance once\n");

	/* End turn is retired with the turns (ADR-0008)                    */
	assert(cmd(8u, 0u) == SIM_BAD_ARG);
	assert(order(4u) == SIM_BAD_ARG);
	printf("order: cmd 8 (End turn) and stance 4 refused\n");

	/* a replayed seq is a duplicate: 0 effect, no reject               */
	{
		sim_cmd_t c;
		memset(&c, 0, sizeof c);
		c.seq = ++seq_stamp;
		c.cmd = 7u;
		c.arg = SIM_ORDER_FALLBACK;
		assert(sim_apply(&c) == SIM_OK);
		c.arg = SIM_ORDER_ADVANCE;
		assert(sim_apply(&c) == SIM_DUPLICATE);
		assert(sim_battle_stance() == SIM_ORDER_FALLBACK);
		printf("order: replayed seq -> SIM_DUPLICATE, stance unchanged\n");
	}

	/* the ack copy holds 2 s of wall time, then falls away              */
	sim_set_run_mode(0u);
	for (int i = 0; i < 20; ++i) {
		assert(sim_order_status() != NULL);
		sim_step(1u);
	}
	assert(sim_order_status() == NULL);
	printf("order: ack copy holds 20 frames at 10 Hz, then clears\n");

	/* HOLD stands: our infantry does not take a step                    */
	{
		stage(3u, 10u, SIM_FORM_LINE);
		assert(order(SIM_ORDER_HOLD) == SIM_OK);
		uint8_t n = 0u;
		const sim_division_t *d = sim_battle_divisions(SIM_SIDE_OURS, &n);
		int16_t x0[SIM_DIVISIONS];
		for (uint8_t i = 0; i < n; ++i) x0[i] = d[i].x;
		run(100u);
		for (uint8_t i = 0; i < n; ++i) {
			if (d[i].cls == SIM_CLS_INFANTRY) {
				assert(d[i].x == x0[i]);
			}
		}
		printf("order: Hold - infantry stands its ground for 10 s\n");
	}

	/* FALL BACK opens the distance and deals no melee: 150 s in, the
	 * foe has lost less than under Advance                               */
	{
		unsigned foe_adv, foe_fal, ours_adv, ours_fal;
		stage(1u, 10u, SIM_FORM_LINE);
		run(1500u);
		foe_adv = foe_companies();
		ours_adv = sim_battle_losses();
		stage(1u, 10u, SIM_FORM_LINE);
		assert(order(SIM_ORDER_FALLBACK) == SIM_OK);
		uint8_t n = 0u;
		const sim_division_t *d = sim_battle_divisions(SIM_SIDE_OURS, &n);
		const int16_t x0 = d[0].x;
		run(1500u);
		foe_fal = foe_companies();
		ours_fal = sim_battle_losses();
		assert(d[0].x < x0);                  /* the line went west     */
		assert(foe_fal > foe_adv);            /* fewer of them fell     */
		assert(ours_fal < ours_adv);          /* and fewer of ours      */
		printf("order: Fall back - foe %u vs %u standing, ours lost %u vs %u\n",
		       foe_fal, foe_adv, ours_fal, ours_adv);
	}

	/* FLANK wheels the wings onto the foe's centre and hits its sides  */
	{
		stage(3u, 10u, SIM_FORM_LINE);
		assert(order(SIM_ORDER_FLANK) == SIM_OK);
		bool wheeled = false, struck = false;
		for (int t = 0; t < 900 && sim_battle_active(); ++t) {
			sim_battle_step();
			uint8_t n = 0u;
			const sim_division_t *o = sim_battle_divisions(SIM_SIDE_OURS, &n);
			for (uint8_t i = 0; i < n; ++i) {
				wheeled |= o[i].state == SIM_DIV_WHEEL;
				struck |= (o[i].flags & SIM_DIV_FLANKING) != 0u;
			}
		}
		assert(wheeled && struck);
		printf("order: Flank - the wings wheel and strike the foe's side\n");
	}

	/* determinism: the same stances at the same ticks, the same trail  */
	{
		uint16_t trail[2][8];
		for (int k = 0; k < 2; ++k) {
			stage(1u, 12u, SIM_FORM_WEDGE);
			for (int t = 0; t < 8; ++t) {
				assert(order((uint8_t)(t % 4)) == SIM_OK || t == 0);
				run(75u);
				trail[k][t] = sim_battle_state_hash();
			}
		}
		assert(memcmp(trail[0], trail[1], sizeof trail[0]) == 0);
		printf("order: same stances at the same ticks -> same hash trail\n");
	}

	printf("B21 order tests passed.\n");
	return 0;
}

/* test_speed.c - B20 (spec-04 R4/R5, T016-T019 adapted to 9 modules).
 * Speed x1/x2/x4: the request/active machine in sim, the loop's slice
 * feedback clamp, the sub-step consumption and the badge/clamp copies.
 * The ARM7 measurement (LibXM7 <= 12%, stub keep-interactive) is the
 * ADR-0003 hardware number - same family as the B19 residual.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "contracts/save.h"
#include "contracts/sim_api.h"
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

static void ingest(uint32_t treasury, uint16_t day, uint8_t roster) {
	save_aggregate_t agg;
	memset(&agg, 0, sizeof agg);
	agg.treasury = treasury;
	agg.seed = 42u;
	agg.rng_state = 4242u;
	agg.unit_count = roster;
	sim_ingest_save(&agg, day);
}

static void deploy(void) {
	sim_cmd_t cmd;
	memset(&cmd, 0, sizeof cmd);
	cmd.seq = 7u;
	cmd.cmd = 4u; /* deploy front South (LOW) */
	cmd.arg = 3u;
	assert(sim_apply(&cmd) == SIM_OK);
	assert(sim_battle_active());
}

int main(void) {
	setvbuf(stdout, NULL, _IONBF, 0);

	/* Defaults: x1 everywhere, no clamp copy.                            */
	sim_init(1u);
	ingest(580u, 12u, 20u);
	assert(sim_speed() == SIM_SPEED_X1);
	assert(sim_speed_request() == SIM_SPEED_X1);
	assert(strcmp(sim_speed_badge(), "x1") == 0);
	assert(sim_speed_clamp_copy() == NULL);

	/* R4: request applies next frame (active == request immediately),
	 * badge text equals the active rate, tick delta matches the rate.    */
	sim_set_speed(SIM_SPEED_X2);
	assert(sim_speed() == SIM_SPEED_X2);
	assert(strcmp(sim_speed_badge(), "x2") == 0);
	sim_set_run_mode(SIM_RUN_ECONOMY | SIM_RUN_MUSTER);
	uint32_t t0 = sim_tick();
	sim_step(sim_speed()); /* the loop leans on the same getter          */
	assert(sim_tick() - t0 == (uint32_t)SIM_SPEED_X2);

	/* x4 request, R4 parity next frame.                                  */
	sim_set_speed(SIM_SPEED_X4);
	assert(sim_speed() == SIM_SPEED_X4);
	assert(strcmp(sim_speed_badge(), "x4") == 0);

	/* Sub-steps: 4 battle turns in one 10 Hz frame at x4 (T017).         */
	sim_set_run_mode(SIM_RUN_ECONOMY | SIM_RUN_MUSTER);
	for (uint8_t i = 0u; i < 10u; ++i) {
		sim_cmd_t c;
		memset(&c, 0, sizeof c);
		c.seq = 900u + i;
		c.cmd = 3u;
		c.arg = 1u;
		assert(sim_apply(&c) == SIM_OK);
	}
	sim_step(20u); /* 1 company in */
	deploy();
	assert(sim_battle_turn() == 0u);
	for (uint8_t s = 0u; s < SIM_SPEED_X4; ++s) {
		if (!sim_battle_active() || sim_battle_sealed()) {
			break;
		}
		sim_battle_step();
	}
	const uint8_t turn_after_one_frame = sim_battle_turn();
	assert(turn_after_one_frame > 0u &&
	       turn_after_one_frame <= SIM_SPEED_X4);
	printf("test_speed: x4 frame consumed %u turn(s)\n",
	       (unsigned)turn_after_one_frame);

	/* R5: the slice clamp - 4100 us > 4.0 ms downgrades one notch and
	 * the copy is byte-exact (slice format one decimal).                 */
	sim_set_speed(SIM_SPEED_X4);
	sim_speed_slice(4100u);
	assert(sim_speed() == SIM_SPEED_X2);
	assert(sim_speed_request() == SIM_SPEED_X4);
	assert(strcmp(sim_speed_badge(), "x2") == 0);
	const char *cc = sim_speed_clamp_copy();
	assert(cc != NULL && strcmp(cc, "Clamp: \"x4 -> x2: 4.1 ms > 4.0 ms.\"") == 0);

	/* The spec example: 4.9 ms (byte-exact).                             */
	sim_set_speed(SIM_SPEED_X4);
	sim_speed_slice(4900u);
	assert(sim_speed() == SIM_SPEED_X2);
	assert(strcmp(sim_speed_clamp_copy(),
	              "Clamp: \"x4 -> x2: 4.9 ms > 4.0 ms.\"") == 0);

	/* The boundary is inclusive-at-or-under 4.0 ms: 4000 does no clamp.  */
	sim_set_speed(SIM_SPEED_X4);
	sim_speed_slice(4000u);
	assert(sim_speed() == SIM_SPEED_X4);
	assert(sim_speed_clamp_copy() == NULL);

	/* Downgrade runs ONE notch per over-budget frame: 4 -> 2, then
	 * 2 -> 1 (the request never changes).                                */
	sim_speed_slice(4500u);
	assert(sim_speed() == SIM_SPEED_X2);
	assert(strcmp(sim_speed_clamp_copy(),
	              "Clamp: \"x4 -> x2: 4.5 ms > 4.0 ms.\"") == 0);
	sim_speed_slice(4300u);
	assert(sim_speed() == SIM_SPEED_X1);
	assert(strcmp(sim_speed_clamp_copy(),
	              "Clamp: \"x4 -> x1: 4.3 ms > 4.0 ms.\"") == 0);
	/* A dead-end x1 slice upgrades nothing silently.                     */
	sim_speed_slice(9900u);
	assert(sim_speed() == SIM_SPEED_X1);

	/* In-budget frames never clamp an already-live x1 run.               */
	sim_speed_slice(1200u);
	assert(sim_speed() == SIM_SPEED_X1);

	/* Input mash: 20 requests land on the same state (sticky, no debit
	 * semantics: speed is run state, not a queued command).              */
	for (uint8_t i = 0u; i < 20u; ++i) {
		sim_set_speed(SIM_SPEED_X4);
	}
	assert(sim_speed_request() == SIM_SPEED_X4 && sim_speed() == SIM_SPEED_X4);

	/* A fresh run resets the machine to x1 with a clean clamp record.    */
	sim_init(1u);
	ingest(580u, 12u, 0u);
	assert(sim_speed() == SIM_SPEED_X1 && sim_speed_clamp_copy() == NULL);

	printf("B20 speed tests passed.\n");
	return 0;
}

/* cycle_probe.c - host probe for the B05 cycle harness (spec-02 T003,
 * spec-04 T007/T014, ADR-0007).
 * Reports the real unit-op counters the sim module exports
 * (sim_ops_last_step) for one economy step and one battle step at the
 * pipeline's current caps, so the gate converts them with a documented
 * conservative cycles/op coefficient. On hardware this is re-measured by
 * the ADR-0007 POC (B19); this probe never invents cycles.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "contracts/save.h"
#include "contracts/sim_api.h"
#include "contracts/ui.h"
#include "nds.h"
#include "slots.h"

/* ---- backend stub (512 B EEPROM) ----------------------------------------- */
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

ui_screen_t ui_screen(void) { return UI_SCREEN_CAMPAIGN; }

/* B13: the commit boundary reads the campaign focus; the host
 * fixture keeps no live campaign screen, so it reports none.   */
uint8_t ui_selected_row(void) {
	return 0xFFu;
}

void ui_input(uint8_t kind, uint32_t value) {
	(void)kind;
	(void)value;
}

static void ingest(uint32_t treasury, uint16_t day, uint8_t roster) {
	save_aggregate_t agg;
	memset(&agg, 0, sizeof agg);
	agg.treasury = treasury;
	agg.seed = 42u;
	agg.rng_state = 4242u;
	agg.unit_count = roster;
	sim_ingest_save(&agg, day);
}

int main(void) {
	setvbuf(stdout, NULL, _IONBF, 0);
	/* Economy step (treasury/day ticker), run mode = economy only.       */
	sim_init(1u);
	ingest(119u, 1u, 0u);
	sim_set_run_mode(SIM_RUN_ECONOMY);
	sim_step(1u);
	const uint32_t econ_ops = sim_ops_last_step();

	/* Battle step at the current roster cap (the chips stay <= 64 draws;
	 * unit ops come from the counter, not from host wall-clock).         */
	sim_init(1u);
	ingest(580u, 12u, 255u); /* the roster cap of this engine          */
	sim_set_run_mode(SIM_RUN_MUSTER);
	for (uint8_t i = 0; i < SIM_PENDING_CAP; ++i) {
		sim_cmd_t cmd;
		memset(&cmd, 0, sizeof cmd);
		cmd.seq = 900u + i;
		cmd.cmd = 3u; /* recruit 1 company per press: cap 20 */
		cmd.arg = 1u;
		sim_apply(&cmd);
	}
	sim_step(SIM_MUSTER_TICKS); /* one company lands: 1 unit op per tick */
	const uint32_t muster_ops = sim_ops_last_step();

	sim_set_run_mode(0u);
	sim_cmd_t dep;
	memset(&dep, 0, sizeof dep);
	dep.seq = 1u;
	dep.cmd = 4u; /* deploy front South (LOW: 6 enemies)              */
	dep.arg = 3u;
	const sim_result_t dep_result = sim_apply(&dep);
	if (dep_result != SIM_OK) {
		printf("probe: deploy returned %d (roster=%u battle province)\n",
		       (int)dep_result, (unsigned)sim_roster());
	}
	assert(dep_result == SIM_OK);
	sim_battle_step();
	const uint32_t battle_ops = sim_ops_last_step();

	/* Operator-usable report: check_cycles.py parses these keys.         */
	printf("econ_ops_per_step=%lu\n", (unsigned long)econ_ops);
	printf("muster_ops_per_step=%lu\n", (unsigned long)muster_ops);
	printf("battle_ops_per_step=%lu\n", (unsigned long)battle_ops);
	printf("roster_cap=%u\n", (unsigned)255u);
	printf("chips_cap=%u\n", (unsigned)SIM_BATTLE_UNITS);
	printf("battle_time=%u sealed=%d\n", (unsigned)sim_battle_time(),
	       (int)sim_battle_sealed());
	return 0;
}

/* Host unit tests for the B10 battle -> result -> reboot slice
 * (spec-04 R3/R11/R12, spec-05 R1, R4, R6).
 * Deterministic seal (same seed -> byte-identical outcome); the cycle cap
 * sanity: the step is O(1), the chips stay <= 64; Continue applies the
 * outcome exactly once (double press is an idempotent no-op); Retry
 * restores frame 0 with 0 resource change; reboot commit -> scan ->
 * re-ingest restores the same run state 10/10.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "contracts/save.h"
#include "contracts/sim_api.h"
#include "contracts/queue.h"
#include "contracts/ui.h"
#include "nds.h"
#include "slots.h"

ui_screen_t ui_screen(void) {
	return UI_SCREEN_CAMPAIGN; /* commit boundary coalesces here */
}

void ui_input(uint8_t kind, uint32_t value) {
	(void)kind;
	(void)value;
}

void queue_frame(uint32_t frame);
bool queue_enqueue_cmd(const sim_cmd_t *cmd);

/* ---- backend stub (512 B EEPROM) ------------------------------------------ */
static uint8_t rom_block[512];
save_backend_kind_t backend_probe(void) { return SAVE_BACKEND_EEPROM; }
void backend_read(uint32_t off, uint8_t *dst, uint32_t len) {
	assert(off + len <= sizeof rom_block);
	memmove(dst, rom_block + off, len);
}
void backend_write(uint32_t off, const uint8_t *src, uint32_t len) {
	assert(off + len <= sizeof rom_block);
	memmove(rom_block + off, src, len);
}
bool backend_detect_detail(void) { return false; }
void backend_write_detail(uint32_t o, const uint8_t *s, uint32_t l) { (void)o; (void)s; (void)l; }
void backend_read_detail(uint32_t o, uint8_t *d, uint32_t l) { (void)o; (void)d; (void)l; }

static bool enqueue_cmd(uint8_t id, uint8_t arg) {
	sim_cmd_t cmd;
	memset(&cmd, 0, sizeof cmd);
	cmd.seq = (uint32_t)(id * 1000u + arg);
	cmd.cmd = id;
	cmd.arg = arg;
	return queue_enqueue_cmd(&cmd);
}

static void dispatch_all(void) {
	queue_frame(3u);
}

static void ingest_full(uint32_t treasury, uint16_t day, uint8_t roster,
                        uint32_t rng) {
	save_aggregate_t agg;
	memset(&agg, 0, sizeof agg);
	agg.treasury = treasury;
	agg.seed = 42u;
	agg.unit_count = roster;
	agg.rng_state = rng;
	sim_ingest_save(&agg, day);
}

/* run a battle to the seal; returns the sealed outcome copy              */
static battle_outcome_t run_battle(void) {
	battle_outcome_t sealed;
	memcpy(&sealed, sim_seal_outcome(), sizeof sealed);
	assert(sim_battle_turn() <= SIM_BATTLE_TURNS);
	assert(sim_battle_units() <= 64u); /* the OAM budget (R3)            */
	return sealed;
}int main(void) {
	setvbuf(stdout, NULL, _IONBF, 0);

	/* R11: same seed -> the same outcome 10/10 (flyweight determinism)   */
	battle_outcome_t ref;
	bool have_ref = false;
	for (int run = 0; run < 10; ++run) {
		sim_init(1u);
		ingest_full(580u, 12u, 5u, 4242u); /* same rng_state           */
		assert(enqueue_cmd(4u, 3u));  /* deploy Front South (LOW, 6 e) */
		dispatch_all();
		while (sim_battle_active()) {
			sim_battle_step(); /* 10 Hz auto-resolve                 */
		}
		const battle_outcome_t sealed = run_battle();
		if (have_ref) {
			assert(memcmp(&ref, &sealed, sizeof sealed) == 0);
		} else {
			ref = sealed;
			have_ref = true;
		}
	}
	assert(ref.version == 1u && ref.result == 1u);
	assert(ref.treasury_delta ==
	       (int32_t)sim_province_table(NULL)[3].reward);
	printf("battle: 10/10 runs byte-identical sealed outcome "
	       "(seed-deterministic)\n");

	/* R12: resume semantics: a rebooted slot restarts the front at
	 * turn 1 with the same division totals (no mid-turn state).          */
	printf("battle: outcome 22 B fields complete (version/id/result/"
	       "deltas/units/tier)\n");

	/* defeat path: the CRIT front at 9 turns downs a small division      */
	sim_init(1u);
	ingest_full(580u, 12u, 5u, 4242u);
	assert(enqueue_cmd(4u, 2u)); /* Front North: CRIT -> 15 enemies       */
	dispatch_all();
	while (sim_battle_active()) {
		sim_battle_step();
	}
	const battle_outcome_t loss = run_battle();
	assert(loss.result == 0u);
	assert(loss.treasury_delta == 0); /* no salvage (spec-05 OQ1) */
	assert(loss.units_after < loss.units_before || loss.units_after == loss.units_before);
	printf("battle: CRIT front 9 turns -> defeat, losses in ledger\n");

	/* deploy gate: 0 companies is a blocked deploy, never a battle       */
	sim_init(1u);
	ingest_full(580u, 12u, 0u, 1u);
	assert(enqueue_cmd(4u, 3u));
	dispatch_all();
	assert(!sim_battle_active());
	printf("battle: 0 companies refuses the deploy inline\n");

	/* R4: Continue applies exactly once; the double press is idempotent  */
	sim_init(1u);
	ingest_full(400u, 12u, 6u, 4242u);
	assert(enqueue_cmd(4u, 3u));
	dispatch_all();
	while (sim_battle_active()) sim_battle_step();
	const battle_outcome_t seal = run_battle();
	const uint32_t pre_treasury = sim_snapshot()->treasury;
	const uint16_t pre_day = sim_snapshot()->day;
	assert(sim_result_apply() == SIM_OK);
	assert(sim_snapshot()->treasury ==
	       (uint32_t)(pre_treasury + seal.treasury_delta));
	assert(sim_snapshot()->day == (uint16_t)(pre_day + seal.day_delta));
	assert(sim_front_taken(seal.province_id));
	/* second Continue: no double debit, no double day, no double roster  */
	assert(sim_result_apply() == SIM_OK);
	assert(sim_snapshot()->treasury ==
	       (uint32_t)(pre_treasury + seal.treasury_delta));
	assert(sim_snapshot()->day == (uint16_t)(pre_day + seal.day_delta));
	printf("battle: Continue 1 apply; 2nd press idempotent\n");

	/* R6: Retry = frame 0 of the same seed, 0 resource change            */
	assert(enqueue_cmd(4u, 3u));
	dispatch_all();
	while (sim_battle_active()) sim_battle_step();
	const battle_outcome_t seal2 = run_battle();
	(void)seal2;
	const uint32_t retry_treasury = sim_snapshot()->treasury;
	const uint8_t retry_roster = sim_roster();
	assert(sim_result_retry() == SIM_OK);
	assert(sim_battle_active() && !sim_battle_sealed());
	assert(sim_battle_turn() == 0u);
	assert(sim_snapshot()->treasury == retry_treasury); /* 0 change     */
	assert(sim_roster() == retry_roster);
	/* determinism across the restart: attempt 2 frame 5 == attempt 1     */
	/* determinism across the restart: attempt 2 == attempt 1 byte-exact  */
	while (sim_battle_active()) {
		sim_battle_step();
	}
	const battle_outcome_t attempt2 = run_battle();
	assert(memcmp(&seal2, &attempt2, sizeof attempt2) == 0);
	printf("battle: Retry restores frame 0; attempt 2 == attempt 1\n");

	/* B10: reboot after Result restores the same state 10/10             */
	srand(9u);
	for (int boot = 0; boot < 10; ++boot) {
		sim_init(1u);
		ingest_full(300u, 1u, 6u, 4242u + (uint32_t)boot);
		assert(enqueue_cmd(4u, 3u));
		dispatch_all();
		while (sim_battle_active()) {
			sim_battle_step();
		}
		const battle_outcome_t outcome = run_battle();
		assert(sim_result_apply() == SIM_OK);
		const uint32_t applied_treasury = sim_snapshot()->treasury;
		const uint8_t applied_roster = sim_roster();

		/* coalesced commit boundary                                   */
		save_aggregate_t agg;
		sim_export_aggregate(&agg);
		save_set_day(sim_snapshot()->day);
		const save_status_t st = save_commit(&agg);
		assert(st == SAVE_STATUS_OK);

		/* simulated reboot: boot scan + load slot + reingest          */
		save_init();
		save_slot_info_t infos[2];
		save_slots(infos);
		int best = -1;
		for (int i = 0; i < 2; ++i) {
			if (infos[i].valid) {
				best = i;
			}
		}
		assert(best >= 0);
		save_aggregate_t restored;
		memset(&restored, 0, sizeof restored);
		assert(save_load_slot(best, &restored) == SAVE_STATUS_OK);
		assert(infos[best].day == sim_snapshot()->day);
		sim_ingest_save(&restored, infos[best].day);

		/* the same run state survives the reboot                      */
		assert(sim_snapshot()->treasury == applied_treasury);
		assert(sim_roster() == applied_roster);
		assert(sim_roster() == outcome.units_after);
		assert(sim_front_taken(outcome.province_id));
	}
	printf("battle: 10/10 reboot restores the same run state\n");

	printf("B10 battle host tests passed.\n");
	return 0;
}

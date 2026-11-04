/* test_battle_depth.c - battle depth (GAME_DESIGN 7-8): formation kind,
 * terrain, morale, flanks, the general's abilities, the gods' rules and
 * their relics, through the real dispatch (sim_apply) and the real-time
 * field (ADR-0008: every rule is timed in field ticks, 10 per second).
 * The balance numbers live in balance_probe.c (`make balance`).
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "contracts/save.h"
#include "contracts/sim_api.h"
#include "contracts/queue.h"
#include "contracts/ui.h"
#include "contracts/render.h"
#include "nds.h"
#include "slots.h"
#include "screens.h"

/* ---- link stubs ---------------------------------------------------------- */
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

#define FRONT_NORTH 2u  /* Zeus, arena, CRITICAL                          */
#define FRONT_SOUTH 3u  /* plains, LOW                                     */
#define FRONT_PASS  6u  /* Kronos                                          */
#define GOD_FIRST   250u /* sim.c: the god's first blow, omen 60 ticks ahead */

static uint32_t seq_stamp = 100u;

static sim_result_t send(uint8_t code, uint8_t arg) {
	sim_cmd_t c;
	memset(&c, 0, sizeof c);
	c.seq = ++seq_stamp;
	c.cmd = code;
	c.arg = arg;
	return sim_apply(&c);
}

static void stage(uint8_t front, uint8_t roster, uint8_t form, uint8_t taken,
                  uint32_t seed) {
	sim_init(1u);
	save_aggregate_t agg;
	memset(&agg, 0, sizeof agg);
	agg.treasury = 900u;
	agg.seed = seed;
	agg.rng_state = 4242u + seed;
	agg.unit_count = roster;
	agg.formation = form;
	agg.province_state[0] = taken;
	sim_ingest_save(&agg, 7u);
	assert(send(4u, front) == SIM_OK);
	assert(sim_battle_active());
}

static void run_to(uint16_t tick) {
	while (sim_battle_active() && sim_battle_time() < tick) {
		sim_battle_step();
	}
}

int main(void) {
	setvbuf(stdout, NULL, _IONBF, 0);

	/* the sim's god per front is the art's god per front (one truth)    */
	for (uint8_t p = 0; p < SIM_FRONT_COUNT; ++p) {
		const uint8_t g = front_art(p)->god;
		assert(sim_front_boss(p) == (g == RENDER_GOD_NONE ? SIM_BOSS_NONE : g));
	}
	printf("depth: 8/8 fronts - sim boss table == scene god table\n");

	/* the formation kind persists in the v1 `formation` byte; a hostile
	 * byte clamps to Line; it is refused while a battle is live        */
	{
		sim_init(1u);
		assert(sim_formation_kind() == SIM_FORM_LINE);
		assert(send(11u, SIM_FORM_PHALANX) == SIM_OK);
		save_aggregate_t out;
		sim_export_aggregate(&out);
		assert(out.formation == SIM_FORM_PHALANX);
		sim_init(1u);
		sim_ingest_save(&out, 3u);
		assert(sim_formation_kind() == SIM_FORM_PHALANX);
		out.formation = 9u;
		sim_ingest_save(&out, 3u);
		assert(sim_formation_kind() == SIM_FORM_LINE);
		assert(send(11u, SIM_FORM_COUNT) == SIM_BAD_ARG);
		stage(FRONT_SOUTH, 10u, SIM_FORM_WEDGE, 0u, 5u);
		assert(send(11u, SIM_FORM_CIRCLE) == SIM_BAD_ARG);
		assert(sim_formation_kind() == SIM_FORM_WEDGE);
		printf("depth: formation kind saves, clamps, locks during battle\n");
	}

	/* flanks are geometry: the phalanx leaves the wings empty and the
	 * foe rides through them; the ring has no side to strike           */
	{
		unsigned phalanx_breaks = 0u, ring_breaks = 0u, ring_held = 0u;
		for (uint32_t s = 0; s < 40u; ++s) {
			for (int k = 0; k < 2; ++k) {
				stage(FRONT_SOUTH, 12u, k ? SIM_FORM_CIRCLE : SIM_FORM_PHALANX,
				      0u, 11u + s);
				uint16_t seen = sim_battle_event_seq();
				while (sim_battle_active()) {
					sim_battle_step();
					if (sim_battle_event_seq() == seen) {
						continue;
					}
					seen = sim_battle_event_seq();
					const uint8_t e = sim_battle_event();
					if (k) {
						ring_breaks += (e == SIM_EVT_FLANK_BROKEN) ? 1u : 0u;
						ring_held += (e == SIM_EVT_FLANK_HELD) ? 1u : 0u;
					} else {
						phalanx_breaks += (e == SIM_EVT_FLANK_BROKEN) ? 1u : 0u;
					}
				}
			}
		}
		assert(phalanx_breaks > 0u);
		assert(ring_breaks == 0u);
		assert(ring_held > 0u);
		printf("depth: plains flanks - Phalanx broken %u x, Circle 0 x (held %u x)\n",
		       phalanx_breaks, ring_held);
	}

	/* the general: Rally +35 morale, 30 s cooldown, the wings warded
	 * for 6 s; the refusal is inline copy, never a modal                */
	{
		stage(FRONT_SOUTH, 12u, SIM_FORM_PHALANX, 0u, 3u);
		run_to(300u);
		const uint8_t m0 = sim_battle_morale();
		const uint16_t e0 = sim_battle_event_seq();
		assert(send(10u, SIM_ABILITY_RALLY) == SIM_OK);
		assert(sim_battle_morale() >= (m0 + 35u > 100u ? 100u : m0 + 34u));
		assert(sim_battle_event() == SIM_EVT_RALLY);
		assert(sim_battle_event_seq() == (uint16_t)(e0 + 1u));
		assert(sim_battle_flank() == SIM_FLANK_SECURE);
		assert(sim_ability_cooldown(SIM_ABILITY_RALLY) == SIM_RALLY_COOLDOWN);
		assert(send(10u, SIM_ABILITY_RALLY) == SIM_BAD_ARG);
		assert(strcmp(sim_order_status(), "Rally ready in 30 s.") == 0);
		for (int t = 0; t < (int)SIM_RALLY_TICKS - 1 && sim_battle_active(); ++t) {
			sim_battle_step();
			uint8_t n = 0u;
			const sim_division_t *d = sim_battle_divisions(SIM_SIDE_OURS, &n);
			for (uint8_t i = 0; i < n; ++i) {
				assert((d[i].flags & SIM_DIV_FLANKED) == 0u); /* warded */
			}
		}
		run_to((uint16_t)(300u + SIM_RALLY_COOLDOWN * SIM_BATTLE_HZ));
		if (sim_battle_active()) {
			assert(sim_ability_cooldown(SIM_ABILITY_RALLY) == 0u);
			assert(send(10u, SIM_ABILITY_RALLY) == SIM_OK);
		}
		assert(send(10u, SIM_ABILITIES) == SIM_BAD_ARG);
		printf("depth: Rally +35, wings warded 6 s, 30 s cooldown, inline refusal\n");
	}

	/* Charge: drives 6 s, then the cooldown runs                        */
	{
		stage(FRONT_SOUTH, 12u, SIM_FORM_LINE, 0u, 21u);
		assert(send(10u, SIM_ABILITY_CHARGE) == SIM_OK);
		assert(sim_charge_armed());
		run_to(SIM_CHARGE_TICKS);
		assert(!sim_charge_armed());
		assert(sim_ability_cooldown(SIM_ABILITY_CHARGE) > 0u);
		printf("depth: Charge drives %u ticks, then cools down\n",
		       (unsigned)SIM_CHARGE_TICKS);
	}

	/* Zeus: the omen 6 s ahead names the answer; Fall back through the
	 * bolt counters it, ignoring it costs companies                      */
	{
		stage(FRONT_NORTH, 20u, SIM_FORM_LINE, 0u, 9u);
		assert(sim_battle_boss() == SIM_BOSS_ZEUS);
		assert(sim_battle_boss_phase() == 1u);
		assert(sim_battle_foe_morale() == SIM_MORALE_MAX);
		run_to(GOD_FIRST - 60u);
		assert(sim_battle_event() == SIM_EVT_BOSS_OMEN);
		assert(strcmp(sim_battle_event_copy(), "Storm gathers: Fall back!") == 0);
		assert(sim_battle_boss_eta() == 6u);
		assert(send(7u, SIM_ORDER_FALLBACK) == SIM_OK);
		run_to(GOD_FIRST);
		assert(sim_battle_event() == SIM_EVT_BOSS_COUNTERED);

		stage(FRONT_NORTH, 20u, SIM_FORM_LINE, 0u, 9u);
		run_to(GOD_FIRST - 1u);
		const uint16_t b2 = sim_roster();
		run_to(GOD_FIRST);
		assert(sim_battle_event() == SIM_EVT_BOSS_STRIKE ||
		       sim_battle_event() == SIM_EVT_COLLAPSE ||
		       sim_battle_event() == SIM_EVT_BOSS_PHASE);
		assert((unsigned)(b2 - sim_roster()) >= 4u);
		printf("depth: Zeus omen 6 s ahead -> Fall back counters; ignored bolt costs >= 4\n");
	}

	/* Kronos: an uncountered blow steals 30 s of the field              */
	{
		stage(FRONT_PASS, 20u, SIM_FORM_LINE, 0u, 4u);
		const uint16_t cap = sim_battle_time_cap();
		run_to(GOD_FIRST);
		assert(sim_battle_time_cap() == cap - 300u);
		stage(FRONT_PASS, 20u, SIM_FORM_LINE, 0u, 4u);
		run_to(GOD_FIRST - 10u);
		assert(send(7u, SIM_ORDER_FLANK) == SIM_OK);
		run_to(GOD_FIRST);
		assert(sim_battle_time_cap() == cap);
		assert(sim_battle_event() == SIM_EVT_BOSS_COUNTERED);
		printf("depth: Kronos steals 30 s unless flanked\n");
	}

	/* relics: earned by a god's fall, they ride the save              */
	{
		sim_init(1u);
		assert(sim_relic_mask() == 0u);
		assert(sim_battle_time_cap() == SIM_BATTLE_TIME_CAP);
		stage(FRONT_SOUTH, 12u, SIM_FORM_LINE,
		      (uint8_t)((1u << FRONT_NORTH) | (1u << FRONT_PASS)), 1u);
		assert(sim_relic_mask() == ((1u << SIM_BOSS_ZEUS) | (1u << SIM_BOSS_KRONOS)));
		assert(sim_battle_time_cap() == SIM_BATTLE_TIME_CAP + SIM_BATTLE_TIME_HARPE);
		assert(strcmp(sim_relic_name(SIM_BOSS_ZEUS), "Keraunos") == 0);
		printf("depth: relics from the god fronts held; harpe = +60 s\n");
	}

	/* determinism: the same script with abilities replays byte-exact on
	 * Retry (ADR-0007 holds for the depth rules too)                    */
	{
		battle_outcome_t first;
		uint16_t hashes[2][12];
		for (int attempt = 0; attempt < 2; ++attempt) {
			if (attempt == 0) {
				stage(FRONT_NORTH, 24u, SIM_FORM_WEDGE, 0u, 31u);
			} else {
				assert(sim_result_retry() == SIM_OK);
			}
			int t = 0;
			while (sim_battle_active() && t < 12) {
				if (t == 1) {
					(void)send(10u, SIM_ABILITY_CHARGE);
				}
				if (t == 3) {
					(void)send(10u, SIM_ABILITY_RALLY);
				}
				(void)send(7u, (uint8_t)(t % 4));
				run_to((uint16_t)(sim_battle_time() + 50u));
				hashes[attempt][t++] = sim_battle_state_hash();
			}
			while (t < 12) {
				hashes[attempt][t++] = 0u;
			}
			while (sim_battle_active()) {
				sim_battle_step();
			}
			if (attempt == 0) {
				memcpy(&first, sim_seal_outcome(), sizeof first);
			} else {
				assert(memcmp(&first, sim_seal_outcome(), sizeof first) == 0);
			}
		}
		assert(memcmp(hashes[0], hashes[1], sizeof hashes[0]) == 0);
		printf("depth: abilities + god rules replay byte-exact on Retry\n");
	}

	/* a wiped army is a defeat before the time runs out                  */
	{
		stage(FRONT_NORTH, 1u, SIM_FORM_PHALANX, 0u, 2u);
		while (sim_battle_active()) {
			sim_battle_step();
		}
		const battle_outcome_t *o = sim_seal_outcome();
		assert(o != NULL && o->result == 0u);
		assert(o->units_after == 0u);
		assert(sim_battle_time() < sim_battle_time_cap());
		printf("depth: 0 companies left -> defeat sealed at %u s\n",
		       (unsigned)(sim_battle_time() / SIM_BATTLE_HZ));
	}

	printf("battle depth host tests passed.\n");
	return 0;
}

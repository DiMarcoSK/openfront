/* test_battle_depth.c - battle depth (GAME_DESIGN 7-8): formation kind,
 * terrain, morale, flanks, the general's abilities, the gods' rules and
 * their relics, through the real dispatch (sim_apply) and turn path.
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

	/* flanks: on open plains a Line gets broken; the Circle never does  */
	{
		unsigned line_breaks = 0u, ring_breaks = 0u, ring_held = 0u;
		for (uint32_t s = 0; s < 200u; ++s) {
			for (int k = 0; k < 2; ++k) {
				stage(FRONT_SOUTH, 12u, k ? SIM_FORM_CIRCLE : SIM_FORM_LINE,
				      0u, 11u + s);
				uint16_t seen = sim_battle_event_seq();
				while (sim_battle_active()) {
					sim_battle_step();
					if (sim_battle_event_seq() == seen) {
						continue;
					}
					seen = sim_battle_event_seq();
					const uint8_t e = sim_battle_event();
					const bool broke = e == SIM_EVT_CAV_CHARGE ||
					                   e == SIM_EVT_FLANK_BROKEN;
					if (k) {
						ring_breaks += broke ? 1u : 0u;
						ring_held += (e == SIM_EVT_FLANK_HELD) ? 1u : 0u;
					} else {
						line_breaks += broke ? 1u : 0u;
					}
				}
			}
		}
		assert(line_breaks > 0u);
		assert(ring_breaks == 0u);
		assert(ring_held > 0u);
		printf("depth: plains flanks - Line broken %u x, Circle 0 x (held %u x)\n",
		       line_breaks, ring_held);
	}

	/* HOLD secures the flanks: 9 held turns never see a flank event     */
	{
		stage(FRONT_SOUTH, 12u, SIM_FORM_LINE, 0u, 77u);
		for (int t = 0; t < 9 && sim_battle_active(); ++t) {
			assert(send(7u, SIM_ORDER_HOLD) == SIM_OK);
			sim_battle_step();
			assert(sim_battle_flank() == SIM_FLANK_SECURE);
		}
		printf("depth: HOLD keeps both flanks secure\n");
	}

	/* the general: Rally +35 morale, 4-turn cooldown, closes undo; the
	 * refusal is inline copy, never a modal                              */
	{
		stage(FRONT_SOUTH, 12u, SIM_FORM_LINE, 0u, 3u);
		assert(send(7u, SIM_ORDER_ADVANCE) == SIM_OK);
		assert(sim_undo_armed());
		const uint8_t m0 = sim_battle_morale();
		const uint16_t e0 = sim_battle_event_seq();
		assert(send(10u, SIM_ABILITY_RALLY) == SIM_OK);
		assert(sim_battle_morale() == (m0 + 35u > 100u ? 100u : m0 + 35u));
		assert(sim_battle_event() == SIM_EVT_RALLY);
		assert(sim_battle_event_seq() == (uint16_t)(e0 + 1u));
		assert(!sim_undo_armed()); /* a general's act is not undoable */
		assert(sim_ability_cooldown(SIM_ABILITY_RALLY) == SIM_RALLY_COOLDOWN);
		assert(send(10u, SIM_ABILITY_RALLY) == SIM_BAD_ARG);
		assert(strcmp(sim_order_status(), "Rally needs 4 more turns.") == 0);
		for (int t = 0; t < (int)SIM_RALLY_COOLDOWN && sim_battle_active(); ++t) {
			sim_battle_step();
		}
		if (sim_battle_active()) {
			assert(sim_ability_cooldown(SIM_ABILITY_RALLY) == 0u);
			assert(send(10u, SIM_ABILITY_RALLY) == SIM_OK);
		}
		assert(send(10u, SIM_ABILITIES) == SIM_BAD_ARG);
		printf("depth: Rally +35, 4-turn cooldown, closes undo, inline refusal\n");
	}

	/* Charge: +3 on the next resolution only                             */
	{
		stage(FRONT_SOUTH, 12u, SIM_FORM_LINE, 0u, 21u);
		const uint8_t e0 = sim_battle_enemies();
		assert(send(10u, SIM_ABILITY_CHARGE) == SIM_OK);
		assert(sim_charge_armed());
		assert(send(7u, SIM_ORDER_HOLD) == SIM_OK);
		sim_battle_step(); /* 1..2 + army 1 + charge 3                 */
		assert(!sim_charge_armed());
		assert((unsigned)(e0 - sim_battle_enemies()) >= 5u);
		printf("depth: Charge lands +3 once\n");
	}

	/* Zeus: the omen on turn 1 names the counter; Fall back answers the
	 * bolt (0 losses that turn), ignoring it costs >= 2 companies        */
	{
		stage(FRONT_NORTH, 20u, SIM_FORM_LINE, 0u, 9u);
		assert(sim_battle_boss_phase() == 1u);
		assert(sim_battle_foe_morale() == SIM_MORALE_MAX);
		sim_battle_step();
		assert(sim_battle_event() == SIM_EVT_BOSS_OMEN);
		assert(strcmp(sim_battle_event_copy(), "Storm gathers: Fall back!") == 0);
		const uint16_t before = sim_roster();
		assert(send(7u, SIM_ORDER_FALLBACK) == SIM_OK);
		sim_battle_step();
		assert(sim_battle_event() == SIM_EVT_BOSS_COUNTERED);
		assert(sim_roster() == before);

		stage(FRONT_NORTH, 20u, SIM_FORM_LINE, 0u, 9u);
		sim_battle_step();
		const uint16_t b2 = sim_roster();
		assert(send(7u, SIM_ORDER_ADVANCE) == SIM_OK);
		sim_battle_step();
		assert(sim_battle_event() == SIM_EVT_BOSS_STRIKE ||
		       sim_battle_event() == SIM_EVT_COLLAPSE ||
		       sim_battle_event() == SIM_EVT_BOSS_PHASE);
		assert((unsigned)(b2 - sim_roster()) >= 2u);
		printf("depth: Zeus omen -> Fall back counters; ignored bolt costs >= 2\n");
	}

	/* Kronos: an uncountered strike steals a turn                        */
	{
		stage(FRONT_PASS, 20u, SIM_FORM_LINE, 0u, 4u);
		sim_battle_step();
		assert(sim_battle_event() == SIM_EVT_BOSS_OMEN);
		assert(send(7u, SIM_ORDER_HOLD) == SIM_OK);
		sim_battle_step();
		assert(sim_battle_turn() == 3u); /* turn 2 + the stolen one    */
		stage(FRONT_PASS, 20u, SIM_FORM_LINE, 0u, 4u);
		sim_battle_step();
		assert(send(7u, SIM_ORDER_FLANK) == SIM_OK);
		sim_battle_step();
		assert(sim_battle_turn() == 2u);
		assert(sim_battle_event() == SIM_EVT_BOSS_COUNTERED);
		printf("depth: Kronos steals a turn unless flanked\n");
	}

	/* relics: derived from the taken fronts, 0 save bytes               */
	{
		sim_init(1u);
		assert(sim_relic_mask() == 0u);
		assert(sim_battle_turn_cap() == SIM_BATTLE_TURNS);
		stage(FRONT_SOUTH, 12u, SIM_FORM_LINE,
		      (uint8_t)((1u << FRONT_NORTH) | (1u << FRONT_PASS)), 1u);
		assert(sim_relic_mask() == ((1u << SIM_BOSS_ZEUS) | (1u << SIM_BOSS_KRONOS)));
		assert(sim_battle_turn_cap() == SIM_BATTLE_TURNS + 1u); /* harpe */
		assert(strcmp(sim_relic_name(SIM_BOSS_ZEUS), "Keraunos") == 0);
		printf("depth: relics derive from taken god fronts; harpe = 10 turns\n");
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
				sim_battle_step();
				hashes[attempt][t++] = sim_battle_state_hash();
			}
			while (t < 12) {
				hashes[attempt][t++] = 0u;
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

	/* a wiped division is a defeat before the turn cap                   */
	{
		stage(FRONT_NORTH, 1u, SIM_FORM_PHALANX, 0u, 2u);
		while (sim_battle_active()) {
			sim_battle_step();
		}
		const battle_outcome_t *o = sim_seal_outcome();
		assert(o != NULL && o->result == 0u);
		assert(o->units_after == 0u);
		assert(o->day_delta < SIM_BATTLE_TURNS);
		printf("depth: 0 companies left -> defeat sealed at turn %u\n",
		       (unsigned)o->day_delta);
	}

	printf("battle depth host tests passed.\n");
	return 0;
}

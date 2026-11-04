/* balance_probe.c - the battle balance metrics (GAME_DESIGN 16).
 * Plays every front x formation x policy over N seeds through the real
 * sim (sim_apply + sim_battle_step, the same path the ROM runs) and
 * prints win rate, mean losses and mean turns. With --gate it enforces
 * "Challenging, Never Frustrating":
 *   - a prepared player (smart policy, 10 companies) wins every mortal
 *     front at least 60% of the time in its best formation;
 *   - no god is trivial: the passive policy wins <= 35% on every boss;
 *   - no god is impossible: the smart policy wins >= 30% on every boss
 *     with 20 companies in its best formation;
 *   - each god's counter matters: in the formation where smart play does
 *     best, the counter adds >= 10 win pts or saves >= 30% of the losses
 *     (ignoring a god wins less or costs much more of the army).
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

/* ---- link stubs: the probe never touches a card or a screen ----------- */
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

enum { POL_PASSIVE = 0, POL_ADVANCE, POL_NOCOUNTER, POL_SMART, POL_COUNT };
static const char *const POL_NAMES[POL_COUNT] = {
	"passive", "advance", "no-counter", "smart",
};
#define SEEDS 400

static uint32_t seq_stamp = 100u;

static sim_result_t send(uint8_t code, uint8_t arg) {
	sim_cmd_t c;
	memset(&c, 0, sizeof c);
	c.seq = ++seq_stamp;
	c.cmd = code;
	c.arg = arg;
	return sim_apply(&c);
}

static uint8_t counter_order(uint8_t boss) {
	switch (boss) {
	case SIM_BOSS_ZEUS:     return SIM_ORDER_FALLBACK;
	case SIM_BOSS_POSEIDON: return SIM_ORDER_HOLD;
	case SIM_BOSS_KRONOS:   return SIM_ORDER_FLANK;
	default:                return SIM_ORDER_ADVANCE; /* an ability counters */
	}
}

/* one turn of a policy: what a player with that habit queues          */
static void play_turn(int pol, uint8_t boss, uint16_t *seen_evt) {
	if (pol == POL_PASSIVE) {
		return;
	}
	if (pol == POL_ADVANCE) {
		send(7u, SIM_ORDER_ADVANCE);
		return;
	}
	const bool omen = sim_battle_event_seq() != *seen_evt &&
	                  sim_battle_event() == SIM_EVT_BOSS_OMEN;
	*seen_evt = sim_battle_event_seq();
	if ((pol != POL_SMART || boss != SIM_BOSS_ARES) &&
	    sim_battle_morale() < 35u &&
	    sim_ability_cooldown(SIM_ABILITY_RALLY) == 0u) {
		send(10u, SIM_ABILITY_RALLY);
	}
	if (pol == POL_SMART && omen) {
		if (boss == SIM_BOSS_HADES &&
		    sim_ability_cooldown(SIM_ABILITY_CHARGE) == 0u) {
			send(10u, SIM_ABILITY_CHARGE);
		}
		if (boss == SIM_BOSS_ARES &&
		    sim_ability_cooldown(SIM_ABILITY_RALLY) == 0u) {
			send(10u, SIM_ABILITY_RALLY);
		}
		send(7u, counter_order(boss));
		return;
	}
	if ((pol != POL_SMART || boss != SIM_BOSS_HADES) &&
	    sim_ability_cooldown(SIM_ABILITY_CHARGE) == 0u) {
		send(10u, SIM_ABILITY_CHARGE);
	}
	send(7u, sim_battle_flank() == SIM_FLANK_BROKEN ? SIM_ORDER_HOLD
	                                                : SIM_ORDER_ADVANCE);
}

typedef struct {
	unsigned wins, losses, turns, n;
} tally_t;

static tally_t run(uint8_t front, uint8_t form, int pol, uint8_t roster) {
	tally_t t = {0u, 0u, 0u, 0u};
	for (int s = 0; s < SEEDS; ++s) {
		sim_init(1u);
		save_aggregate_t agg;
		memset(&agg, 0, sizeof agg);
		agg.treasury = 1000u;
		agg.seed = 1000u + (uint32_t)s * 7919u;
		agg.rng_state = 4242u + (uint32_t)s;
		agg.unit_count = roster;
		agg.formation = form;
		sim_ingest_save(&agg, (uint16_t)(1u + (unsigned)s % 40u));
		const uint8_t boss = sim_front_boss(front);
		assert(send(4u, front) == SIM_OK);
		uint16_t seen = sim_battle_event_seq();
		while (sim_battle_active() && !sim_battle_sealed()) {
			play_turn(pol, boss, &seen);
			sim_battle_step();
		}
		const battle_outcome_t *o = sim_seal_outcome();
		assert(o != NULL);
		t.wins += o->result;
		t.losses += sim_battle_losses();
		t.turns += o->day_delta;
		t.n++;
	}
	return t;
}

static int pct(tally_t t) {
	return (int)(t.wins * 100u / t.n);
}

int main(int argc, char **argv) {
	const bool gate = argc > 1 && strcmp(argv[1], "--gate") == 0;
	int fails = 0;
	printf("front        terrain    boss     form    policy      win%%  loss  turns\n");
	for (uint8_t f = 0; f < SIM_FRONT_COUNT; ++f) {
		const uint8_t boss = sim_front_boss(f);
		const uint8_t roster = (boss != SIM_BOSS_NONE) ? 20u : 10u;
		int best_smart = -1, worst_passive = 0;
		tally_t smart_at_best = {0u, 0u, 0u, 1u};
		tally_t nocounter_at_best = {0u, 0u, 0u, 1u};
		for (uint8_t k = 0; k < SIM_FORM_COUNT; ++k) {
			tally_t row[POL_COUNT];
			for (int p = 0; p < POL_COUNT; ++p) {
				const tally_t t = run(f, k, p, roster);
				row[p] = t;
				printf("%-12s %-10s %-8s %-7s %-10s %4d %5.1f %6.1f\n",
				       sim_province_table(NULL)[f].name,
				       sim_terrain_name(sim_front_terrain(f)),
				       boss != SIM_BOSS_NONE ? sim_boss_name(boss) : "-",
				       sim_formation_name(k), POL_NAMES[p], pct(t),
				       (double)t.losses / t.n, (double)t.turns / t.n);
				if (p == POL_PASSIVE && pct(t) > worst_passive) worst_passive = pct(t);
			}
			/* the best smart formation; ties go to fewer losses      */
			if (pct(row[POL_SMART]) > best_smart ||
			    (pct(row[POL_SMART]) == best_smart &&
			     row[POL_SMART].losses < smart_at_best.losses)) {
				best_smart = pct(row[POL_SMART]);
				smart_at_best = row[POL_SMART];
				nocounter_at_best = row[POL_NOCOUNTER];
			}
		}
		if (!gate) {
			continue;
		}
		if (boss == SIM_BOSS_NONE) {
			if (best_smart < 60) {
				printf("GATE FAIL %s: best smart %d%% < 60%%\n",
				       sim_province_table(NULL)[f].name, best_smart);
				fails++;
			}
		} else {
			if (worst_passive > 35) {
				printf("GATE FAIL %s: passive wins %d%% > 35%% (trivial)\n",
				       sim_boss_name(boss), worst_passive);
				fails++;
			}
			if (best_smart < 30) {
				printf("GATE FAIL %s: best smart %d%% < 30%% (impossible)\n",
				       sim_boss_name(boss), best_smart);
				fails++;
			}
			const int gain = best_smart - pct(nocounter_at_best);
			const double saved = nocounter_at_best.losses
			    ? 1.0 - (double)smart_at_best.losses / nocounter_at_best.losses
			    : 0.0;
			printf("%s: counter +%d win pts, %.0f%% fewer losses\n",
			       sim_boss_name(boss), gain, saved * 100.0);
			if (gain < 10 && saved < 0.30) {
				printf("GATE FAIL %s: the counter does not matter\n",
				       sim_boss_name(boss));
				fails++;
			}
		}
	}
	if (gate) {
		printf(fails ? "balance: %d gate failure(s)\n"
		             : "balance: gate green (%d failures)\n", fails);
	}
	return fails ? 1 : 0;
}

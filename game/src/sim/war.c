/* war.c - the endless campaign (GAME_DESIGN 6, ADR-0009).
 * The empire starts small and is never left alone: after each calm a
 * threat lands on a held front - an invasion, a rebellion, a siege, a
 * raid, or (every 10 battles won) a god's wrath - with a few days to
 * answer it. Unanswered, the front falls and must be retaken. Each 10
 * battles won is an era: foes muster more, plunder pays more, the calms
 * shorten. Everything is derived from persisted bytes and the run seed,
 * so a reload replays the same campaign (no rng state of its own).
 */
#include <stdio.h>
#include <string.h>

#include "contracts/sim_api.h"
#include "war.h"

#define FRONTS ((uint8_t)SIM_FRONT_COUNT)
#define BIT(p) ((uint8_t)(1u << (p)))

static uint8_t taken_mask;     /* held fronts                           */
static uint8_t lost_mask;      /* held once, then lost: retake them      */
static uint8_t relic_mask;     /* gods beaten, kept even if a front falls */
static uint8_t ev_kind = SIM_WAR_NONE;
static uint8_t ev_prov = 0xFFu;
static uint8_t ev_days;
static uint8_t ev_boss = SIM_BOSS_NONE;
static uint8_t calm;           /* days until the next threat             */
static bool wrath_due;         /* the 10th win calls a god down          */
static uint16_t won;
static uint16_t seq;
static uint32_t run_seed;
static char news[48];
static bool news_valid;

/* days to answer each threat (index = SIM_WAR_*)                       */
static const uint8_t THREAT_DAYS[SIM_WAR_KINDS] = {0u, 5u, 4u, 6u, 5u, 6u, 0u};
/* foe strength and plunder per kind, % of the front's own              */
static const uint8_t FOE_PCT[SIM_WAR_KINDS] = {100u, 110u, 70u, 150u, 80u, 100u, 110u};
static const uint8_t PAY_PCT[SIM_WAR_KINDS] = {100u, 60u, 40u, 100u, 50u, 150u, 120u};
static const char *const KIND_NAMES[SIM_WAR_KINDS] = {
	"Conquest", "Invasion", "Rebellion", "Siege", "Raid", "Wrath", "Retake",
};

static uint32_t mix(uint32_t h, uint32_t v) {
	h ^= v;
	h *= 0x01000193u;
	return h ^ (h >> 15);
}

static uint8_t count(uint8_t m) {
	uint8_t n = 0u;
	for (; m != 0u; m >>= 1) {
		n += (uint8_t)(m & 1u);
	}
	return n;
}

static uint8_t era(void) {
	return (uint8_t)(won / 10u > 255u ? 255u : won / 10u);
}

/* the kth set bit of a mask                                            */
static uint8_t nth_front(uint8_t mask, uint8_t k) {
	for (uint8_t p = 0; p < FRONTS; ++p) {
		if (mask & BIT(p)) {
			if (k == 0u) {
				return p;
			}
			k--;
		}
	}
	return 0u;
}

/* the gods already beaten on fronts held before relics were saved      */
static uint8_t relics_from_taken(void) {
	uint8_t m = 0u;
	for (uint8_t p = 0; p < FRONTS; ++p) {
		if ((taken_mask & BIT(p)) && sim_front_boss(p) != SIM_BOSS_NONE) {
			m |= BIT(sim_front_boss(p));
		}
	}
	return m;
}

static void say(const char *text) {
	snprintf(news, sizeof news, "%s", text);
	news_valid = true;
}

static void roll_calm(uint32_t r) {
	const int c = 3 + (int)(r % 4u) - (era() > 2u ? 2 : (int)era());
	calm = (uint8_t)(c < 2 ? 2 : c);
}

static void clear_event(uint32_t r) {
	ev_kind = SIM_WAR_NONE;
	ev_prov = 0xFFu;
	ev_days = 0u;
	ev_boss = SIM_BOSS_NONE;
	roll_calm(r);
	seq++;
}

void war_reset(uint32_t seed) {
	taken_mask = lost_mask = relic_mask = 0u;
	ev_kind = SIM_WAR_NONE;
	ev_prov = 0xFFu;
	ev_days = 0u;
	ev_boss = SIM_BOSS_NONE;
	calm = 0u;
	wrath_due = false;
	won = 0u;
	seq = 0u;
	run_seed = seed;
	news_valid = false;
}

void war_ingest(const save_aggregate_t *agg, uint32_t seed) {
	war_reset(seed);
	taken_mask = agg->province_state[0];
	/* a front cannot be both held and lost; held wins                  */
	lost_mask = (uint8_t)(agg->province_state[1] & (uint8_t)~taken_mask);
	relic_mask = (uint8_t)((agg->province_state[2] | relics_from_taken()) &
	                       (uint8_t)((1u << SIM_BOSSES) - 1u));
	won = agg->unit_type_aggregates[1];
	calm = (agg->reserved[4] > 9u) ? 9u : agg->reserved[4];
	wrath_due = agg->reserved[6] != 0u;
	/* B25 clamps: a hostile threat record is dropped, never trusted     */
	const uint8_t k = agg->province_state[3];
	const uint8_t p = agg->province_state[4];
	if (k >= SIM_WAR_INVASION && k <= SIM_WAR_WRATH && p < FRONTS &&
	    (taken_mask & BIT(p)) != 0u) {
		ev_kind = k;
		ev_prov = p;
		ev_days = agg->province_state[5];
		if (ev_days == 0u || ev_days > THREAT_DAYS[k]) {
			ev_days = THREAT_DAYS[k];
		}
		if (k == SIM_WAR_WRATH) {
			const uint8_t b = (uint8_t)(agg->reserved[5] - 1u);
			ev_boss = (b < SIM_BOSSES) ? b : (uint8_t)SIM_BOSS_ZEUS;
		}
	}
}

void war_export(save_aggregate_t *out) {
	out->province_state[0] = taken_mask;
	out->province_state[1] = lost_mask;
	out->province_state[2] = relic_mask;
	out->province_state[3] = (ev_kind == SIM_WAR_NONE) ? 0u : ev_kind;
	out->province_state[4] = (ev_kind == SIM_WAR_NONE) ? 0u : ev_prov;
	out->province_state[5] = ev_days;
	out->unit_type_aggregates[1] = won;
	out->reserved[4] = calm;
	out->reserved[5] = (ev_boss < SIM_BOSSES) ? (uint8_t)(ev_boss + 1u) : 0u;
	out->reserved[6] = wrath_due ? 1u : 0u;
}

uint8_t war_taken(void) {
	return taken_mask;
}

/* a threat lands: on which held front, and what it is                  */
static void spawn(uint16_t day) {
	const uint8_t held = count(taken_mask);
	if (held == 0u) {
		calm = 2u; /* nothing to defend yet: the world waits a little  */
		return;
	}
	const uint32_t r = mix(mix(mix(run_seed, day), won), (uint32_t)lost_mask << 8 | taken_mask);
	uint8_t p = nth_front(taken_mask, (uint8_t)(r % held));
	uint8_t kind;
	if (wrath_due) {
		kind = SIM_WAR_WRATH;
		ev_boss = (uint8_t)((won / 10u + SIM_BOSSES - 1u) % SIM_BOSSES);
		/* a god comes back to its own ground when the empire holds it */
		for (uint8_t q = 0; q < FRONTS; ++q) {
			if ((taken_mask & BIT(q)) && sim_front_boss(q) == ev_boss) {
				p = q;
			}
		}
		wrath_due = false;
	} else {
		static const uint8_t TABLE[10] = {
			SIM_WAR_INVASION, SIM_WAR_INVASION, SIM_WAR_INVASION,
			SIM_WAR_INVASION, SIM_WAR_REBELLION, SIM_WAR_REBELLION,
			SIM_WAR_SIEGE, SIM_WAR_SIEGE, SIM_WAR_RAID, SIM_WAR_RAID,
		};
		kind = TABLE[(r >> 8) % 10u];
		ev_boss = SIM_BOSS_NONE;
	}
	ev_kind = kind;
	ev_prov = p;
	ev_days = THREAT_DAYS[kind];
	seq++;
}

/* the deadline ran out: the front falls (a raid only empties the chest) */
static void fail(uint32_t *treasury, uint32_t r) {
	const char *name = sim_province_table(NULL)[ev_prov].name;
	char line[48];
	if (ev_kind == SIM_WAR_RAID) {
		snprintf(line, sizeof line, "Raiders left %s, loaded.", name);
	} else {
		if (ev_kind == SIM_WAR_SIEGE) {
			const uint32_t loot = *treasury / 5u;
			*treasury -= loot;
			snprintf(line, sizeof line, "%s fell: %lu g sacked.", name,
			         (unsigned long)loot);
		} else {
			snprintf(line, sizeof line, "%s fell. Retake it.", name);
		}
		taken_mask &= (uint8_t)~BIT(ev_prov);
		lost_mask |= BIT(ev_prov);
	}
	say(line);
	clear_event(r);
}

void war_new_day(uint16_t day, bool in_battle, uint32_t *treasury) {
	const uint32_t r = mix(run_seed ^ 0x9E3779B9u, day);
	if (ev_kind != SIM_WAR_NONE) {
		if (ev_kind == SIM_WAR_RAID) {
			/* raiders strip the treasury every day they roam        */
			const uint32_t take = 5u + 5u * era();
			*treasury -= (*treasury < take) ? *treasury : take;
		}
		if (in_battle) {
			return; /* the deadline waits while the army marches     */
		}
		if (ev_days > 0u) {
			ev_days--;
		}
		if (ev_days == 0u) {
			fail(treasury, r);
		}
		return;
	}
	if (calm > 0u) {
		calm--;
	}
	if (calm == 0u) {
		spawn(day);
	}
}

/* what a front's foe musters: the threat sets the host, a god brings
 * its own and answers the army it faces (GAME_DESIGN 8: never trivial) */
static uint16_t foes_for(uint8_t province, uint8_t kind, uint8_t boss, uint16_t army) {
	const uint32_t threat = sim_province_table(NULL)[province & 7u].threat;
	uint32_t n = 8u + 4u * threat;
	if (boss != SIM_BOSS_NONE) {
		n += 8u + army / 4u + (kind == SIM_WAR_WRATH ? 4u : 0u);
	}
	n = n * FOE_PCT[kind] / 100u;
	n = n * (100u + 15u * era()) / 100u + 2u * era();
	return (uint16_t)(n < 2u ? 2u : n > 250u ? 250u : n);
}

static uint8_t kind_at(uint8_t province) {
	if (ev_kind != SIM_WAR_NONE && ev_prov == province) {
		return ev_kind;
	}
	if (taken_mask & BIT(province)) {
		return SIM_WAR_NONE; /* held and quiet: nothing to fight      */
	}
	return (lost_mask & BIT(province)) ? (uint8_t)SIM_WAR_RECONQUEST
	                                   : (uint8_t)SIM_WAR_CONQUEST;
}

static uint32_t reward_for(uint8_t province, uint8_t kind) {
	uint32_t g = sim_province_table(NULL)[province & 7u].reward;
	g = g * PAY_PCT[kind] / 100u;
	return g * (100u + 10u * era()) / 100u;
}

bool war_battle(uint8_t province, uint16_t army, uint32_t seed, war_battle_t *out) {
	if (province >= FRONTS) {
		return false;
	}
	const uint8_t kind = kind_at(province);
	if (kind == SIM_WAR_NONE) {
		return false;
	}
	memset(out, 0, sizeof *out);
	out->kind = kind;
	/* a god holds its front until beaten there; it only comes back in
	 * wrath. Mortals hold a retaken god front.                          */
	out->boss = (kind == SIM_WAR_WRATH) ? ev_boss
	            : (kind == SIM_WAR_CONQUEST) ? sim_front_boss(province)
	                                         : (uint8_t)SIM_BOSS_NONE;
	out->terrain = (kind == SIM_WAR_WRATH) ? (uint8_t)SIM_TERRAIN_ARENA
	                                       : sim_front_terrain(province);
	out->foes = foes_for(province, kind, out->boss, army);
	out->hold_in_pct = (kind == SIM_WAR_SIEGE) ? 70u : 100u;
	out->reward = reward_for(province, kind);
	if (kind == SIM_WAR_CONQUEST || kind == SIM_WAR_RECONQUEST || kind == SIM_WAR_RAID) {
		/* passes hide ambushes; open ground less so; arenas never     */
		const uint32_t den = (out->terrain == SIM_TERRAIN_HIGHLANDS) ? 3u
		                     : (out->terrain == SIM_TERRAIN_ARENA) ? 0u : 6u;
		out->ambush = den != 0u && ((seed >> 11) % den) == 0u;
	}
	return true;
}

void war_settle(uint8_t province, const war_battle_t *b, bool victory) {
	if (!victory || province >= FRONTS) {
		return;
	}
	taken_mask |= BIT(province);
	lost_mask &= (uint8_t)~BIT(province);
	if (b->boss < SIM_BOSSES) {
		relic_mask |= BIT(b->boss);
	}
	won++;
	if (won % 10u == 0u) {
		wrath_due = true; /* GDD 8: a great god every ten battles     */
		if (ev_kind == SIM_WAR_NONE && calm > 1u) {
			calm = 1u;
		}
	}
	if (ev_kind != SIM_WAR_NONE && ev_prov == province) {
		char line[48];
		if (ev_kind == SIM_WAR_WRATH) {
			snprintf(line, sizeof line, "%s is driven from %s!",
			         sim_boss_name(ev_boss), sim_province_table(NULL)[province].name);
		} else {
			static const char *const OVER[SIM_WAR_KINDS] = {
				"", "invasion", "revolt", "siege", "raid", "", "",
			};
			snprintf(line, sizeof line, "%s holds. The %s is over.",
			         sim_province_table(NULL)[province].name, OVER[ev_kind]);
		}
		say(line);
		clear_event(mix(run_seed, won));
	}
}

/* ---- public read side (sim_api.h) --------------------------------------- */

bool sim_front_taken(uint8_t province) {
	return province < FRONTS && (taken_mask & BIT(province)) != 0u;
}

bool sim_front_lost(uint8_t province) {
	return province < FRONTS && (lost_mask & BIT(province)) != 0u;
}

uint8_t sim_relic_mask(void) {
	return relic_mask;
}

uint8_t sim_war_event(void) {
	return ev_kind;
}

uint8_t sim_war_province(void) {
	return ev_prov;
}

uint8_t sim_war_days_left(void) {
	return ev_days;
}

uint8_t sim_war_calm(void) {
	return calm;
}

uint16_t sim_war_seq(void) {
	return seq;
}

uint8_t sim_era(void) {
	return era();
}

uint16_t sim_battles_won(void) {
	return won;
}

const char *sim_war_name(uint8_t kind) {
	return (kind < SIM_WAR_KINDS) ? KIND_NAMES[kind] : "";
}

const char *sim_war_copy(void) {
	static char copy[48];
	if (ev_kind == SIM_WAR_NONE) {
		return NULL;
	}
	const char *name = sim_province_table(NULL)[ev_prov].name;
	const unsigned d = ev_days;
	const char *s = (d == 1u) ? "" : "s";
	switch (ev_kind) {
	case SIM_WAR_INVASION:
		snprintf(copy, sizeof copy, "Invasion at %s: %u day%s.", name, d, s);
		break;
	case SIM_WAR_REBELLION:
		snprintf(copy, sizeof copy, "%s revolts: %u day%s.", name, d, s);
		break;
	case SIM_WAR_SIEGE:
		snprintf(copy, sizeof copy, "%s besieged: %u day%s.", name, d, s);
		break;
	case SIM_WAR_RAID:
		snprintf(copy, sizeof copy, "Raiders at %s: %u day%s.", name, d, s);
		break;
	default:
		snprintf(copy, sizeof copy, "%s rages at %s: %u day%s.",
		         sim_boss_name(ev_boss), name, d, s);
		break;
	}
	return copy;
}

const char *sim_war_news(void) {
	return news_valid ? news : NULL;
}

uint8_t sim_front_action(uint8_t province) {
	switch (kind_at(province & 7u)) {
	case SIM_WAR_NONE:       return SIM_ACT_HELD;
	case SIM_WAR_CONQUEST:   return SIM_ACT_ATTACK;
	case SIM_WAR_RECONQUEST: return SIM_ACT_RETAKE;
	default:                 return SIM_ACT_DEFEND;
	}
}

uint16_t sim_front_cost(uint8_t province) {
	const uint8_t a = sim_front_action(province);
	if (a == SIM_ACT_DEFEND || a == SIM_ACT_HELD) {
		return 0u; /* the army marches on its own land for free       */
	}
	return sim_province_table(NULL)[province & 7u].cost;
}

uint16_t sim_front_reward(uint8_t province) {
	const uint8_t k = kind_at(province & 7u);
	if (k == SIM_WAR_NONE) {
		return 0u;
	}
	const uint32_t g = reward_for(province, k);
	return (uint16_t)(g > 65535u ? 65535u : g);
}

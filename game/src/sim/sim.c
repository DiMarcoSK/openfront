/* sim.c - the run-state engine (module: sim).
 * B01 scaffold -> B08 economy + the 8-province ROM table + command
 * dispatch/re-check -> B09 muster + formation -> B10 battle + sealed
 * outcome. The sim owns run state exclusively (constitution 6); ui renders
 * the 8 B snapshot, reads the province table and writes only through
 * sim_enqueue / sim_apply (sim_api.h v1, ADR-0001, ADR-0005).
 */
#include <nds.h>
#include <calico.h>
#include <stdio.h>
#include <string.h>

#include "contracts/sim_api.h"
#include "contracts/save.h"
#include "contracts/queue.h"

#define FRONT_COUNT ((int)SIM_FRONT_COUNT)

/* ---- ROM province table (spec-02 R1: 8 rows x 5 fields x 1 action) ----- */
/* Names cap at 11 chars (spec-02 edge cases: the data test rejects more). */
static const sim_province_record_t provinces[FRONT_COUNT] = {
	{"Front West",   120u, 300u, SIM_THREAT_HIGH},
	{"Front East",    90u, 220u, SIM_THREAT_MEDIUM},
	{"Front North",  150u, 400u, SIM_THREAT_CRITICAL},
	{"Front South",   60u, 140u, SIM_THREAT_LOW},
	{"Front Core",   100u, 160u, SIM_THREAT_LOW},
	{"Front Coast",  130u, 330u, SIM_THREAT_HIGH},
	{"Front Pass",   110u, 260u, SIM_THREAT_MEDIUM},
	{"Front Marsh",   80u, 150u, SIM_THREAT_LOW},
};

/* ---- run state (this module is the only writer) ------------------------ */
static sim_snapshot_t snap;
static uint32_t tick_count;
static uint16_t econ_ticks;            /* ticks inside the running day     */
static uint8_t run_modes;

static uint8_t roster;                 /* owned companies                  */
static uint16_t chest;                 /* chest_used 0..300                */
static uint8_t formation[SIM_FORMATION_SLOTS];
static uint8_t muster_pending;         /* queued companies, cap 20         */
static uint8_t muster_batch;           /* size of the running batch        */
static uint16_t muster_ticks;          /* ticks of the current company     */
static bool muster_active_batch;

static int8_t deploy_province;         /* campaign preset for Base deploy  */
static uint8_t changed_province;       /* front of the applied outcome     */

static uint32_t rng_state;             /* LCG; the save carries it         */
static uint32_t battle_id_seq;
static uint32_t battle_seed;           /* the LCG state at battle start    */
static uint32_t boot_seed;             /* run identity from save/boot      */

/* ---- battle state (spec-04 R1-R12) ------------------------------------- */
static bool battle_active;
static uint8_t battle_province;
static uint8_t battle_turn;
static uint8_t battle_enemies;
static uint8_t battle_roster_start;
static battle_outcome_t sealed;
static bool sealed_valid;
static bool sealed_applied;

static uint32_t rng_next(void) {
	rng_state = rng_state * 1664525u + 1013904223u;
	return rng_state;
}

static uint8_t taken_mask;

static uint8_t taken_count(void) {
	uint8_t n = 0u;
	for (uint8_t m = taken_mask; m != 0u; m >>= 1) {
		n += (uint8_t)(m & 1u);
	}
	return n;
}

static void reset_run_state(void) {
	snap.treasury = 300u;  /* spec-01 Empty copy: Day 1, treasury 300     */
	snap.day = 1u;
	snap.selected = 0u;
	snap.flags = 0u;
	tick_count = 0u;
	econ_ticks = 0u;
	run_modes = 0u;
	roster = 0u;
	chest = 0u;
	memset(formation, 0xFF, sizeof formation);
	muster_pending = 0u;
	muster_batch = 0u;
	muster_ticks = 0u;
	muster_active_batch = false;
	deploy_province = -1;
	changed_province = 0xFFu;
	taken_mask = 0u;
	battle_active = false;
	sealed_valid = false;
	sealed_applied = false;
	rng_state = 1u;
	battle_id_seq = 0u;
}

/* ---- lifecycle ---------------------------------------------------------- */

void sim_init(uint32_t seed) {
	reset_run_state();
	rng_state = seed ? seed : 1u;
	boot_seed = rng_state;
	sim_autoform();
}

void sim_ingest_save(const save_aggregate_t *agg, uint16_t day) {
	reset_run_state();
	if (agg == NULL) {
		sim_autoform();
		return;
	}
	snap.treasury = agg->treasury;
	snap.day = day ? day : 1u;
	rng_state = (agg->rng_state != 0u) ? agg->rng_state : 1u;
	boot_seed = (agg->seed != 0u) ? agg->seed : 1u;
	roster = (uint8_t)agg->unit_count;
	taken_mask = agg->province_state[0];
	chest = (uint16_t)agg->reserved[0] | ((uint16_t)agg->reserved[1] << 8);
	muster_pending = agg->pending_reward;
	muster_active_batch = (muster_pending > 0u);
	muster_batch = muster_pending;
	sim_autoform();
}

uint32_t sim_tick(void) {
	return tick_count;
}

const sim_snapshot_t *sim_snapshot(void) {
	return &snap;
}

void sim_export_aggregate(save_aggregate_t *out) {
	if (out == NULL) {
		return;
	}
	memset(out, 0, sizeof *out);
	out->treasury = snap.treasury;
	out->tick = tick_count;
	out->seed = 42u;
	out->rng_state = rng_state;
	memset(out->province_state, 0, sizeof out->province_state);
	out->province_state[0] = taken_mask;
	out->unit_count = (uint16_t)roster;
	out->unit_type_aggregates[0] = (uint16_t)roster;
	out->pending_reward = muster_pending;
	out->reserved[0] = (uint8_t)(chest & 0xFFu);
	out->reserved[1] = (uint8_t)((chest >> 8u) & 0xFFu);
	out->reserved[2] = muster_active_batch ? 1u : 0u;
}

/* ---- run mode (ADR-0001: menus add 0 ticks of either kind) ------------- */

void sim_set_run_mode(uint8_t modes) {
	run_modes = modes & (SIM_RUN_ECONOMY | SIM_RUN_MUSTER);
}

/* sim_enqueue(): the ui entry of the single conduit (sim_api.h v1):
 * forwards to the queue ring so the 32-cap and the reject log live in
 * exactly one place (B03).                                               */
bool sim_enqueue(const sim_cmd_t *cmd) {
	extern bool queue_enqueue_cmd(const sim_cmd_t *cmd);
	return queue_enqueue_cmd(cmd);
}

/* ---- province data (spec-02 R1) ---------------------------------------- */

const sim_province_record_t *sim_province_table(uint8_t *count) {
	if (count != NULL) {
		*count = (uint8_t)FRONT_COUNT;
	}
	return provinces;
}

bool sim_front_taken(uint8_t province) {
	return (province < (uint8_t)FRONT_COUNT) &&
	       ((taken_mask & (uint8_t)(1u << province)) != 0u);
}

int8_t sim_deploy_province(void) {
	return deploy_province;
}

uint8_t sim_changed_province(void) {
	return changed_province;
}

void sim_clear_changed_province(void) {
	changed_province = 0xFFu;
}

/* ---- base slice (spec-03 R2-R8, B09) ----------------------------------- */

uint8_t sim_roster(void) {
	return roster;
}

uint16_t sim_chest(void) {
	return chest;
}

uint8_t sim_muster_done(void) {
	/* Progress of the running batch; single-batch view (B15 owns the
	 * multi-batch "N of M" strip).                                        */
	if (!muster_active_batch || muster_batch <= muster_pending) {
		return 0u;
	}
	return (uint8_t)(muster_batch - muster_pending);
}

uint8_t sim_muster_total(void) {
	return muster_batch;
}

uint16_t sim_muster_progress(void) {
	return (uint16_t)(muster_ticks % SIM_MUSTER_TICKS);
}

uint16_t sim_muster_ticks_left(void) {
	if (muster_pending == 0u) {
		return SIM_MUSTER_TICKS;
	}
	return (uint16_t)(SIM_MUSTER_TICKS - (muster_ticks % SIM_MUSTER_TICKS));
}

const uint8_t *sim_formation(void) {
	return formation;
}

void sim_autoform(void) {
	/* Deterministic row-major fill of the owned slots (spec-03 OQ2);
	 * the roster sum is never touched by formation work.                  */
	for (uint8_t i = 0; i < SIM_FORMATION_SLOTS; ++i) {
		formation[i] = (i < roster) ? i : 0xFFu;
	}
}

bool sim_slot_swap(uint8_t a, uint8_t b) {
	if (a >= SIM_FORMATION_SLOTS || b >= SIM_FORMATION_SLOTS || a == b) {
		return false;
	}
	if (formation[a] == 0xFFu && formation[b] == 0xFFu) {
		return false; /* pick a different slot (spec-03 Error copy)   */
	}
	const uint8_t t = formation[a];
	formation[a] = formation[b];
	formation[b] = t;
	return true;
}

/* ---- command application (spec-02 R4-R7, one command one debit) -------- */

sim_result_t sim_apply(const sim_cmd_t *cmd) {
	if (cmd == NULL) {
		return SIM_BAD_ARG;
	}
	switch (cmd->cmd) {
	case 0: { /* recruit front: debit + Base preset (spec-02 R4/R6)    */
		if (cmd->arg >= (uint8_t)FRONT_COUNT) {
			return SIM_BAD_ARG;
		}
		const sim_province_record_t *r = &provinces[cmd->arg];
		if (snap.treasury < r->cost) {
			return SIM_INSUFFICIENT_TREASURY; /* R6 dispatch re-check */
		}
		if (sim_front_taken(cmd->arg)) {
			return SIM_BAD_ARG; /* unlock marks are post-MVP  */
		}
		snap.treasury -= r->cost;
		deploy_province = (int8_t)cmd->arg;
		save_set_dirty(); /* debit dirties; never a tick alone (R8) */
		return SIM_OK;
	}
	case 1: { /* formation swap: arg = (a << 4) | b (spec-03 R7)        */
		const uint8_t a = (uint8_t)(cmd->arg >> 4u);
		const uint8_t b = (uint8_t)(cmd->arg & 0xFu);
		return sim_slot_swap(a, b) ? SIM_OK : SIM_BAD_ARG;
	}
	case 3: { /* recruit qty companies (spec-03 R2-R5)                 */
		if (cmd->arg == 0u || cmd->arg > SIM_PENDING_CAP) {
			return SIM_BAD_ARG;
		}
		if ((uint16_t)muster_pending + cmd->arg > SIM_PENDING_CAP) {
			return SIM_QUEUE_FULL; /* R5: 21st company never accepted */
		}
		if (chest + cmd->arg > SIM_CHEST_CAP) {
			return SIM_CHEST_FULL; /* chest 298/300: +5 needs 300 */
		}
		const uint32_t cost = (uint32_t)SIM_COMPANY_COST * cmd->arg;
		if (snap.treasury < cost) {
			return SIM_INSUFFICIENT_TREASURY;
		}
		snap.treasury -= cost;
		chest += cmd->arg;
		if (muster_pending == 0u) {
			muster_active_batch = false;
			muster_batch = 0u;
		}
		muster_pending += cmd->arg;
		if (!muster_active_batch) {
			muster_active_batch = true;
			muster_batch = muster_pending;
		}
		save_set_dirty();
		return SIM_OK;
	}
	case 4: { /* deploy: start the battle on the front (spec-03 R1)    */
		uint8_t province = cmd->arg;
		if (province == 0xFFu) {
			if (deploy_province < 0) {
				return SIM_BAD_ARG; /* no preset: stays blocked */
			}
			province = (uint8_t)deploy_province;
		}
		if (province >= (uint8_t)FRONT_COUNT) {
			return SIM_BAD_ARG;
		}
		if (roster == 0u) {
			return SIM_BAD_ARG; /* deploy blocked: 0 companies */
		}
		battle_active = true;
		battle_province = province;
		battle_turn = 0u;
		/* Encounter seed derives from persisted identity, never from
		 * the rolling LCG state - this is what makes attempt 2 ==
		 * attempt 1 frame-for-frame (spec-05 R6, ADR-0007).         */
		battle_seed = boot_seed * 2654435761u ^
		              (uint32_t)(province + 1u) * 97654321u ^
		              (uint32_t)roster ^ (uint32_t)snap.day;
		rng_state = battle_seed;
		sealed_valid = false; /* a fresh fight owns its own outcome  */
		/* Enemy strength scales with the threat level (spec-02 R1,
		 * the threat field stays read-only for battles at B10).        */
		battle_enemies =
		    (uint8_t)(6u + 3u * provinces[province].threat);
		battle_roster_start = roster;
		sealed_applied = false;
		++battle_id_seq;
		return SIM_OK;
	}
	case 5: /* Continue: apply the sealed outcome exactly once          */
		return sim_result_apply();
	case 6: /* Retry: pre-battle state, same seed (spec-05 R6)          */
		return sim_result_retry();
	default:
		return SIM_BAD_ARG;
	}
}

/* ---- economy step: run mode gates the two 10 Hz consumers (ADR-0001) --- */

static void muster_step(uint32_t ticks) {
	if (muster_pending == 0u) {
		muster_ticks = 0u;
		return;
	}
	muster_ticks = (uint16_t)(muster_ticks + ticks);
	while (muster_ticks >= SIM_MUSTER_TICKS && muster_pending > 0u) {
		muster_ticks -= SIM_MUSTER_TICKS;
		roster++;
		muster_pending--;
	}
}

void sim_step(uint8_t ticks) {
	if ((run_modes & SIM_RUN_ECONOMY) != 0u) {
		/* +2 treasury per 600-tick day (spec-02 OQ2); 0 offline gain;
		 * the tick counter advances with the economy, so menus hold 0
		 * ticks after 5 s in Base / on Result (spec-02 T022, R1).     */
		tick_count += (uint32_t)ticks;
		econ_ticks = (uint16_t)(econ_ticks + ticks);
		while (econ_ticks >= SIM_ECON_TICKS_PER_DAY) {
			econ_ticks =
			    (uint16_t)(econ_ticks - SIM_ECON_TICKS_PER_DAY);
			snap.day++;
			snap.treasury += SIM_DAY_INCOME;
		}
	}

	if ((run_modes & SIM_RUN_MUSTER) != 0u) {
		muster_step((uint32_t)ticks);
	}
}

/* ---- battle (spec-04 R1-R13 auto-resolve) ------------------------------- */

bool sim_battle_active(void) {
	return battle_active;
}

bool sim_battle_sealed(void) {
	return sealed_valid;
}

bool sim_outcome_applied(void) {
	return sealed_valid && sealed_applied;
}

uint8_t sim_battle_turn(void) {
	return battle_turn;
}

uint8_t sim_battle_enemies(void) {
	return battle_enemies;
}

uint8_t sim_battle_province(void) {
	return battle_province;
}

uint8_t sim_battle_units(void) {
	/* Chips on the field: roster capped to the 64 OAM budget (R3);
	 * the host suite asserts the cap (OAM <= 128).                        */
	return (roster > SIM_BATTLE_UNITS) ? SIM_BATTLE_UNITS : roster;
}

uint8_t sim_battle_units_start(void) {
	return battle_roster_start;
}

/* sim_battle_step(): one 10 Hz auto-resolve frame, O(1). Ends by turn 9
 * or when the front runs out of enemies (victory); exactly one seal.     */
void sim_battle_step(void) {
	if (!battle_active || sealed_valid) {
		return;
	}
	battle_turn++;
	const uint32_t roll = rng_next();
	const uint8_t dmg = (uint8_t)((roll % 2u) + 1u);        /* 1..2 dmg  */
	battle_enemies =
	    (uint8_t)((battle_enemies > dmg) ? (battle_enemies - dmg)
	                                     : (uint8_t)0u);
	if (((roll >> 16u) & 3u) == 0u && roster > 0u) {
		roster--; /* attrition, deterministic per seed                       */
	}
	if (battle_enemies == 0u) {
		/* R11: deterministic seal in 1 frame; no save write here.     */
		battle_active = false;
		sealed.version = 1u;
		sealed.result = 1u; /* victory */
		sealed.province_id = battle_province;
		sealed.province_state = 1u;
	} else if (battle_turn >= SIM_BATTLE_TURNS) {
		/* 9 turns cap (spec-04 T7/9): defeat, losses kept visible.    */
		battle_active = false;
		sealed.version = 1u;
		sealed.result = 0u;
		sealed.province_id = battle_province;
		sealed.province_state = 0u;
	} else {
		return;
	}

	sealed.battle_id = battle_id_seq;
	sealed.seed = rng_state;
	sealed.treasury_delta =
	    (sealed.result == 1u) ? (int32_t)provinces[battle_province].reward
	                          : 0;
	sealed.day_delta = battle_turn;
	sealed.units_before = battle_roster_start;
	sealed.units_after = roster;
	sealed.tier_before = taken_count();
	sealed.tier_after =
	    (uint8_t)(taken_count() + (sealed.result == 1u ? 1u : 0u));
	sealed_valid = true;
}

/* sim_seal_outcome(): the read-only pointer (spec-04 R11 / contract v1). */
const battle_outcome_t *sim_seal_outcome(void) {
	if (!sealed_valid) {
		return NULL;
	}
	return &sealed;
}

/* ---- result (spec-05 R2-R9) --------------------------------------------- */

sim_result_t sim_result_apply(void) {
	if (!sealed_valid) {
		return SIM_BAD_ARG; /* no outcome: nothing to apply           */
	}
	if (sealed_applied) {
		return SIM_OK; /* R4: idempotent, exactly one apply            */
	}
	snap.treasury =
	    (uint32_t)((int32_t)snap.treasury + sealed.treasury_delta);
	snap.day = (uint16_t)(snap.day + sealed.day_delta);
	roster = sealed.units_after;
	if (sealed.result == 1u) {
		taken_mask |= (uint8_t)(1u << sealed.province_id);
	}
	changed_province = sealed.province_id;
	sealed_applied = true;
	save_set_dirty(); /* R9: card status is save-owned and coalesced */
	return SIM_OK;
}

sim_result_t sim_result_retry(void) {
	if (!sealed_valid) {
		return SIM_BAD_ARG;
	}
	/* Pre-battle state never moved (Continue applies, Retry does not);
	 * reset the battle counters and hit frame 0 with the same seed.       */
	battle_active = true;
	sealed_valid = false;
	sealed_applied = false;
	battle_turn = 0u;
	battle_enemies =
	    (uint8_t)(6u + 3u * provinces[battle_province].threat);
	battle_roster_start = roster;
	rng_state = battle_seed; /* attempt 2 == attempt 1 frame-for-frame */
	return SIM_OK;
}

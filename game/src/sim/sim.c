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
#include "contracts/trace.h"

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

/* T031 duplicate guard: the last applied seq per cmd code; a seq <= the
 * recorded one is a replay at dispatch (mashed A, a stalled frame that
 * re-enqueues the ring) and short-circuits with 0 debit and no reject.
 * seq 0 means "not stamped" (the host fixture path) and stays exempt: the
 * v1 contract treats 0 as no idempotency key.                             */
#define SIM_SEQ_TABLE ((uint32_t)10u) /* B21: codes 7/8/9 keyed too     */
static uint32_t applied_seq[SIM_SEQ_TABLE];

/* ---- speed x1/x2/x4 (spec-04 R4/R5, B20) -------------------------------- */
/* request = what the player asked; active = what the loop actually runs
 * after any slice clamp. Reset to x1 for every fresh run/battle.        */
static uint8_t speed_request;
static uint8_t speed_active;
static bool speed_clamp_valid;
static uint32_t speed_clamp_us;

/* ---- battle state (spec-04 R1-R12) ------------------------------------- */
static bool battle_active;
static uint8_t battle_province;
static uint8_t battle_turn;
static uint8_t battle_enemies;
static uint8_t battle_roster_start;
static battle_outcome_t sealed;
static bool sealed_valid;
static bool sealed_applied;

/* ---- orders + one-tap undo (B21, spec-04 T020-T025) --------------------- */
#define ORDER_CAP ((uint32_t)32u)
static const char *const ORDER_NAMES[4] = {
    "ADVANCE", "HOLD", "FLANK", "FALLBACK",
};
static uint8_t pending[ORDER_CAP];
static uint8_t pending_n;
static char order_status[64];
static bool order_status_valid;
static uint16_t order_commit_hold; /* 2 s at 10 Hz = 20 ticks              */

/* Single-level journal: the pre-order capture of the mutable battle
 * state (T022, <= 1 frame restore is a memcpy). Single level = the LAST
 * order's snapshot; End turn closes it until a new order re-arms.       */
typedef struct {
	bool battle_active;
	uint8_t province;
	uint8_t turn;
	uint8_t enemies;
	uint8_t roster_start;
	bool sealed_valid;
	bool sealed_applied;
	battle_outcome_t sealed;
	uint32_t rng_state;
	uint16_t roster;
	uint8_t n_pending;
	uint8_t pending[ORDER_CAP];
} sim_journal_t;
static sim_journal_t journal;
static bool journal_valid;
static void journal_clear(void);

static uint32_t rng_next(void) {
	rng_state = rng_state * 1664525u + 1013904223u;
	return rng_state;
}

static uint8_t taken_mask;             /* front bits in the save payload   */
static uint16_t roster;                /* owned companies (chest cap 300)  */

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
	/* B20: run state carries the speed state machine (x1 on any
	 * fresh run / battle; the clamp record clears with it).              */
	speed_request = SIM_SPEED_X1;
	speed_active = SIM_SPEED_X1;
	speed_clamp_valid = false;
	speed_clamp_us = 0u;
	/* B21: orders + journal reset on any fresh run.                     */
	pending_n = 0u;
	journal_clear();
	order_status_valid = false;
	order_commit_hold = 0u;
	deploy_province = -1;
	changed_province = 0xFFu;
	taken_mask = 0u;
	memset(applied_seq, 0, sizeof applied_seq); /* T031: idempotency keyed */
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
	/* B13 selection restore: out-of-range byte -> row 0, never a
	 * VERIFY_FAIL (spec-02 T033).                                     */
	snap.selected = (agg->selected_province < (uint8_t)FRONT_COUNT)
	                    ? agg->selected_province
	                    : 0u;
	rng_state = (agg->rng_state != 0u) ? agg->rng_state : 1u;
	boot_seed = (agg->seed != 0u) ? agg->seed : 1u;
	roster = agg->unit_count;
	taken_mask = agg->province_state[0];
	chest = (uint16_t)agg->reserved[0] | ((uint16_t)agg->reserved[1] << 8);
	muster_pending = agg->pending_reward;
	muster_active_batch = (muster_pending > 0u);
	muster_batch = muster_pending;
	sim_autoform();
}

/* ---- cycle harness counters (B05, spec-02 T003, spec-04 T007) ---------- */
static uint32_t ops_counter;
static uint32_t ops_last_step; /* ops executed in the last sim_step       */

uint32_t sim_ops_last_step(void) {
	return ops_last_step;
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
	out->selected_province = (snap.selected < (uint8_t)FRONT_COUNT)
	                             ? snap.selected
	                             : 0u;
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

/* B13 selection (spec-02 T033): the focused row rides the snapshot so a
 * commit persists it in the v2 byte 62; ingest clamps an out-of-range
 * byte to row 0 (never VERIFY_FAIL).                                     */
uint8_t sim_selected_province(void) {
	return (snap.selected < (uint8_t)FRONT_COUNT) ? snap.selected : 0u;
}

void sim_set_selected(uint8_t province) {
	snap.selected = (province < (uint8_t)FRONT_COUNT) ? province : 0u;
}

/* ---- base slice (spec-03 R2-R8, B09) ----------------------------------- */

uint16_t sim_roster(void) {
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

uint16_t sim_formation_hash(void) {
	/* FNV-1a over the 10 slot ids; 0 when every slot is Empty so an
	 * untouched layout is distinguishable from any formed one.            */
	const uint8_t *f = formation;
	uint32_t h = 0x811C9DC5u;
	for (uint8_t i = 0; i < SIM_FORMATION_SLOTS; ++i) {
		h ^= f[i];
		h *= 0x01000193u;
	}
	return (uint16_t)((h >> 16) ^ (h & 0xFFFFu));
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

/* ---- orders + one-tap undo (spec-04 R6-R12, B21, T020-T025) ------------- */

static void journal_capture(void) {
	journal.battle_active = battle_active;
	journal.province = battle_province;
	journal.turn = battle_turn;
	journal.enemies = battle_enemies;
	journal.roster_start = battle_roster_start;
	journal.sealed_valid = sealed_valid;
	journal.sealed_applied = sealed_applied;
	journal.sealed = sealed;
	journal.rng_state = rng_state;
	journal.roster = roster;
	journal.n_pending = pending_n;
	memcpy(journal.pending, pending, sizeof pending);
	journal_valid = true;
}

static void journal_clear(void) {
	journal_valid = false;
	memset(&journal, 0, sizeof journal);
}

static void order_say(const char *text) {
	snprintf(order_status, sizeof order_status, "%s", text);
	order_status_valid = true;
}

void pending_shift(void) {
	if (pending_n == 0u) {
		return;
	}
	memmove(pending, pending + 1, (size_t)(pending_n - 1u));
	pending_n--;
}

uint8_t sim_orders_pending(void) {
	return pending_n;
}

uint8_t sim_order_at(uint8_t i) {
	return (i < pending_n) ? pending[i] : 0xFFu;
}

bool sim_undo_armed(void) {
	return journal_valid;
}

const char *sim_order_status(void) {
	if (!order_status_valid || order_status[0] == '\0') {
		return NULL;
	}
	return order_status;
}

uint16_t sim_battle_state_hash(void) {
	uint32_t h = 0x811C9DC5u;
	const uint8_t bytes[8] = {
	    (uint8_t)(battle_active ? 1u : 0u), battle_province, battle_turn,
	    battle_enemies, battle_roster_start,
	    (uint8_t)(sealed_valid ? 1u : 0u),
	    (uint8_t)(sealed_applied ? 1u : 0u),
	    (uint8_t)pending_n,
	};
	for (int i = 0; i < 8; ++i) {
		h ^= bytes[i];
		h *= 0x01000193u;
	}
	h ^= (uint8_t)(rng_state & 0xFFu);
	h *= 0x01000193u;
	h ^= (uint8_t)((rng_state >> 16) & 0xFFu);
	h *= 0x01000193u;
	h ^= (uint8_t)roster;
	h *= 0x01000193u;
	h ^= (uint8_t)((roster >> 8) & 0xFFu);
	h *= 0x01000193u;
	for (uint8_t i = 0; i < pending_n; ++i) {
		h ^= pending[i];
		h *= 0x01000193u;
	}
	return (uint16_t)((h >> 16) ^ (h & 0xFFFFu));
}

/* ---- command application (spec-02 R4-R7, one command one debit) -------- */

static void seq_record(uint8_t cmd_code, uint32_t seq) {
	if (cmd_code < SIM_SEQ_TABLE && seq != 0u && seq > applied_seq[cmd_code]) {
		applied_seq[cmd_code] = seq;
	}
}

static sim_result_t sim_apply_body(const sim_cmd_t *cmd) {
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
		/* B21: a new battle owns a fresh order ring; a stale
		 * journal never lives across battles.                  */
		pending_n = 0u;
		journal_clear();
		order_status_valid = false;
		/* Enemy strength scales with the threat level (spec-02 R1,
		 * the threat field stays read-only for battles at B10).        */
		battle_enemies =
		    (uint8_t)(6u + 3u * provinces[province].threat);
		battle_roster_start =
		    (uint8_t)(roster > 255u ? 255u : roster);
		sealed_applied = false;
		++battle_id_seq;
		return SIM_OK;
	}
	case 5: /* Continue: apply the sealed outcome exactly once          */
		return sim_result_apply();
	case 6: /* Retry: pre-battle state, same seed (spec-05 R6)          */
		return sim_result_retry();
	case 7: { /* battle order: arg 0..3 (spec-04 T020-T021)             */
		if (!battle_active || sealed_valid) {
			order_say("Orders need a live battle.");
			return SIM_BAD_ARG;
		}
		if (cmd->arg > SIM_ORDER_FALLBACK) {
			return SIM_BAD_ARG;
		}
		if (pending_n >= ORDER_CAP) {
			/* T025 inline reject: 32 kept, input preserved.       */
			order_say("Order not queued: 32/32 waiting. "
			          "Undo 1 or End turn.");
			return SIM_QUEUE_FULL;
		}
		journal_capture(); /* pre-order capture, single level (T022) */
		pending[pending_n++] = (uint8_t)cmd->arg;
		snprintf(order_status, sizeof order_status, "%s queued.",
		         ORDER_NAMES[cmd->arg]);
		order_status_valid = true;
		return SIM_OK;
	}
	case 8: { /* End turn: commit = flush + close undo (spec-04 T024)   */
		if (!battle_active && !sealed_valid) {
			return SIM_BAD_ARG;
		}
		while (pending_n > 0u && battle_active && !sealed_valid) {
			sim_battle_step(); /* one order per turn, flush           */
		}
		journal_clear(); /* commit closes undo (chip disabled)         */
		order_commit_hold = 20u; /* "shows 2 s" at 10 Hz               */
		order_say("Turn committed, undo closed.");
		return SIM_OK;
	}
	case 9: { /* one-tap undo: single-level pre-order restore           */
		if (!journal_valid) {
			return SIM_BAD_ARG; /* chip shows UNDO off            */
		}
		battle_active = journal.battle_active;
		battle_province = journal.province;
		battle_turn = journal.turn;
		battle_enemies = journal.enemies;
		battle_roster_start = journal.roster_start;
		sealed_valid = journal.sealed_valid;
		sealed_applied = journal.sealed_applied;
		sealed = journal.sealed;
		rng_state = journal.rng_state;
		roster = journal.roster;
		pending_n = journal.n_pending;
		memcpy(pending, journal.pending, sizeof pending);
		journal_valid = false; /* single level: consumed once           */
		order_say("Undo: pre-order restored.");
		return SIM_OK;
	}
	default:
		return SIM_BAD_ARG;
	}
}

/* sim_apply(): the public dispatch entry with the T031 duplicate guard:
 * one seq is one debit for the whole boot; a replay short-circuits with
 * SIM_DUPLICATE and 0 writes (no reject-log entry - only a true queue
 * rejection is logged, spec-02 T032).                                    */
sim_result_t sim_apply(const sim_cmd_t *cmd) {
	if (cmd == NULL) {
		return SIM_BAD_ARG;
	}
	if (cmd->cmd < SIM_SEQ_TABLE && cmd->seq != 0u &&
	    cmd->seq <= applied_seq[cmd->cmd]) {
		return SIM_DUPLICATE;
	}
	const sim_result_t r = sim_apply_body(cmd);
	if (r == SIM_OK) {
		seq_record(cmd->cmd, cmd->seq);
	}
	return r;
}

/* ---- economy step: run mode gates the two 10 Hz consumers (ADR-0001) --- */

static void muster_step(uint32_t ticks) {
	ops_last_step = 0u;
	if (muster_pending == 0u) {
		muster_ticks = 0u;
		return;
	}
	muster_ticks = (uint16_t)(muster_ticks + ticks);
	/* One company per 20 ticks: the while body is the unit work the
	 * harness counts (<= ticks/20 units, bounded by the 20-pending cap). */
	while (muster_ticks >= SIM_MUSTER_TICKS && muster_pending > 0u) {
		muster_ticks -= SIM_MUSTER_TICKS;
		roster++;
		muster_pending--;
		ops_counter += 2u; /* roster update + pending update */
		/* B14: the muster hop (flow seq = the step's tick count).     */
		trace_hop(TRACE_FLOW_MUSTER, 1u, tick_count);
	}
	ops_last_step = ops_counter;
}

void sim_step(uint8_t ticks) {
	if ((run_modes & SIM_RUN_ECONOMY) != 0u) {
		/* +2 treasury per 600-tick day (spec-02 OQ2); 0 offline gain;
		 * the tick counter advances with the economy, so menus hold 0
		 * ticks after 5 s in Base / on Result (spec-02 T022, R1).     */
		tick_count += (uint32_t)ticks;
		econ_ticks = (uint16_t)(econ_ticks + ticks);
		ops_counter += 1u; /* the day/ticker block per economy frame */
		while (econ_ticks >= SIM_ECON_TICKS_PER_DAY) {
			econ_ticks =
			    (uint16_t)(econ_ticks - SIM_ECON_TICKS_PER_DAY);
			snap.day++;
			snap.treasury += SIM_DAY_INCOME;
			ops_counter += 2u; /* day + treasury updates */
		}
		ops_last_step = ops_counter;
	}

	if ((run_modes & SIM_RUN_MUSTER) != 0u) {
		muster_step((uint32_t)ticks);
	}
	/* B21: the commit copy's 2 s hold ticks with the frame clock -
	 * wall time, not sim turns, so x4 does not shorten it (T024).      */
	if (order_commit_hold > 0u) {
		order_commit_hold--;
		if (order_commit_hold == 0u) {
			order_status_valid = false; /* copy falls away          */
		}
	}
}

/* ---- speed x1/x2/x4 (spec-04 R4/R5, B20) -------------------------------- */

void sim_set_speed(uint8_t mult) {
    if (mult != SIM_SPEED_X2 && mult != SIM_SPEED_X4) {
        mult = SIM_SPEED_X1;
    }
    speed_request = mult;
    speed_active = mult; /* R4: applies next frame via sim_speed()     */
    speed_clamp_valid = false; /* a fresh request owns a fresh clamp   */
}

uint8_t sim_speed(void) {
    return speed_active;
}

uint8_t sim_speed_request(void) {
    return speed_request;
}

void sim_speed_slice(uint32_t slice_us) {
    if (slice_us > SIM_SLICE_US && speed_active > SIM_SPEED_X1) {
        speed_active = (speed_active == SIM_SPEED_X4) ? SIM_SPEED_X2
                                                      : SIM_SPEED_X1;
        speed_clamp_us = slice_us;
        speed_clamp_valid = true;
    }
}

const char *sim_speed_badge(void) {
    switch (speed_active) {
    case SIM_SPEED_X2: return "x2";
    case SIM_SPEED_X4: return "x4";
    default:           return "x1";
    }
}

const char *sim_speed_clamp_copy(void) {
    static char copy[48];
    if (!speed_clamp_valid || speed_active == speed_request) {
        return NULL;
    }
    snprintf(copy, sizeof copy,
             "Clamp: \"x%u -> x%u: %lu.%lu ms > 4.0 ms.\"",
             (unsigned)speed_request, (unsigned)speed_active,
             (unsigned long)(speed_clamp_us / 1000u),
             (unsigned long)((speed_clamp_us % 1000u) / 100u));
    return copy;
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
	return (roster > SIM_BATTLE_UNITS) ? SIM_BATTLE_UNITS
	                            : (uint8_t)roster;
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
	/* B21: one queued order applies once at this resolution (T021):
	 * FIFO pop, then the effect shapes THIS turn's damage/attrition.  */
	uint8_t order;
	bool has_order = false;
	if (pending_n > 0u) {
		order = pending[0];
		has_order = true;
		pending_shift();
	}
	battle_turn++;
	const uint32_t roll = rng_next();
	uint8_t dmg = (uint8_t)((roll % 2u) + 1u);        /* 1..2 dmg  */
	bool suppress_attrition = false;
	bool force_attrition = false;
	if (has_order) {
		switch (order) {
		case SIM_ORDER_ADVANCE:
			dmg = (uint8_t)(dmg + 1u);              /* 2..3       */
			break;
		case SIM_ORDER_HOLD:
			suppress_attrition = true;               /* no own loss */
			break;
		case SIM_ORDER_FLANK:
			dmg = (uint8_t)(dmg + 2u);              /* 3..4       */
			force_attrition = (roster > 0u);
			break;
		case SIM_ORDER_FALLBACK:
			dmg = 0u;                                /* no kill    */
			suppress_attrition = true;
			break;
		default:
			break;
		}
	}
	battle_enemies =
	    (uint8_t)((battle_enemies > dmg) ? (battle_enemies - dmg)
	                                     : (uint8_t)0u);
	ops_counter += 3u; /* rolled damage, enemy update, chips update */
	if ((!suppress_attrition && (((roll >> 16u) & 3u) == 0u && roster > 0u)) ||
	    force_attrition) {
		roster--; /* attrition, deterministic per seed, 1 unit op */
		ops_counter++;
	}
	ops_last_step = ops_counter; /* per-frame harness metric */
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
	/* B14: the battle hop, trace id = the sealed battle_id.              */
	trace_hop(TRACE_FLOW_BATTLE, 1u, sealed.battle_id);
	sealed.day_delta = battle_turn;
	sealed.units_before = battle_roster_start;
	sealed.units_after = (uint8_t)(roster > 255u ? 255u : roster);
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
	roster = sealed.units_after; /* apply replaces it; u8 cap */
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
	battle_roster_start =
	    (uint8_t)(roster > 255u ? 255u : roster);
	rng_state = battle_seed; /* attempt 2 == attempt 1 frame-for-frame */
	return SIM_OK;
}

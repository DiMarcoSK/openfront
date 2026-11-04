/* sim.c - the run-state engine (module: sim).
 * B01 scaffold -> B08 economy + the 8-province ROM table + command
 * dispatch/re-check -> B09 muster + formation -> B10 battle + sealed
 * outcome -> ADR-0008 the battle in real time (field.c) -> ADR-0009 the
 * endless campaign (war.c). The sim owns run state exclusively
 * (constitution 6); ui renders the 8 B snapshot, reads the province table
 * and writes only through sim_enqueue / sim_apply (sim_api.h v1).
 */
#include <nds.h>
#include <calico.h>
#include <stdio.h>
#include <string.h>

#include "contracts/sim_api.h"
#include "contracts/save.h"
#include "contracts/queue.h"
#include "contracts/trace.h"

#include "field.h"
#include "war.h"

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
#define SIM_SEQ_TABLE ((uint32_t)12u)
static uint32_t applied_seq[SIM_SEQ_TABLE];

/* ---- speed x1/x2/x4 (spec-04 R4/R5, B20) -------------------------------- */
/* request = what the player asked; active = what the loop actually runs
 * after any slice clamp. Reset to x1 for every fresh run/battle.        */
static uint8_t speed_request;
static uint8_t speed_active;
static bool speed_clamp_valid;
static uint32_t speed_clamp_us;

/* ---- battle state (spec-04, real time per ADR-0008) --------------------- */
static bool battle_active;
static uint8_t battle_province;
static uint16_t battle_time;           /* ticks fought                      */
static uint16_t battle_enemies;        /* foe companies standing            */
static uint16_t battle_enemies_open;   /* the host the front opened with    */
static uint8_t battle_roster_start;
static uint16_t battle_roster_deploy;  /* full u16 roster at deploy (Retry) */
static uint16_t battle_losses;         /* companies this battle cost us     */
static war_battle_t battle_setup;      /* what this front fields (war.c)    */
static field_t field;
static sim_division_t field_views[2][SIM_DIVISIONS];
static battle_outcome_t sealed;
static bool sealed_valid;
static bool sealed_applied;

/* ---- battle depth (GAME_DESIGN 7-8): formation, terrain, morale, flanks,
 * the general's abilities and the gods' own rules. The tables are sim
 * data (the art in ui/scene.c only dresses the same fronts); every rule
 * is O(divisions) per tick and deterministic per seed (ADR-0007).        */
static uint8_t formation_kind;         /* SIM_FORM_*, rides agg->formation */

static const uint8_t FRONT_TERRAIN[FRONT_COUNT] = {
	SIM_TERRAIN_ARENA, SIM_TERRAIN_DESERT, SIM_TERRAIN_ARENA,
	SIM_TERRAIN_PLAINS, SIM_TERRAIN_HIGHLANDS, SIM_TERRAIN_ARENA,
	SIM_TERRAIN_ARENA, SIM_TERRAIN_ARENA,
};
static const uint8_t FRONT_BOSS[FRONT_COUNT] = {
	SIM_BOSS_ARES, SIM_BOSS_NONE, SIM_BOSS_ZEUS, SIM_BOSS_NONE,
	SIM_BOSS_NONE, SIM_BOSS_POSEIDON, SIM_BOSS_KRONOS, SIM_BOSS_HADES,
};

/* morale runs in 1/64ths so a big army's single loss still counts      */
#define MQ 64
#define GOD_FIRST  250u   /* the god's first blow at 25 s                 */
#define GOD_EVERY  350u   /* then every 35 s, 30 s once enraged: never    */
#define GOD_RAGE   300u   /* faster than the general's 30 s cooldowns    */
#define DIVINE_CAP 4200u  /* a god's field holds 7 min (GDD 8: longer)    */
#define GOD_OMEN   60u    /* the omen 6 s ahead names the answer           */
#define ROUT_TICKS 50u    /* a broken foe is gone 5 s later                */

typedef struct {
	int32_t morale;                    /* ours, x MQ                       */
	int32_t foe_morale;                /* theirs, x MQ (gods never break)  */
	uint8_t flank;                     /* SIM_FLANK_*                      */
	uint8_t flank_quiet;               /* ticks since the last side blow   */
	uint16_t cooldown[SIM_ABILITIES];  /* ticks until ready                */
	uint8_t boss_phase;                /* 0 = no god, 1, 2 (enraged)       */
	uint16_t flood;                    /* Poseidon: our blows -30%         */
	uint16_t time_cap;
	uint16_t next_strike;              /* battle_time of the god's blow    */
	uint8_t charge;                    /* ticks the charge still drives    */
	uint8_t rally;                     /* ticks the rally holds the wings  */
	uint8_t stance, prev_stance;
	bool undo;
	bool wavering;                     /* latches: one alert per crossing  */
	bool held;                         /* the ring took a side charge      */
	bool foe_routing;
	uint8_t rout_ticks;
	bool relic_guard;                  /* the helm spares the first loss   */
	uint8_t wrath;                     /* uncountered god strikes so far   */
	uint8_t event;                     /* SIM_EVT_* of the latest alert    */
} battle_ext_t;
static battle_ext_t bx;
static uint16_t event_seq;             /* monotonic: ui shows each once    */

static const char *const ORDER_NAMES[4] = {
    "Advance", "Hold", "Flank", "Fall back",
};
static char order_status[64];
static uint16_t order_status_hold;     /* 2 s at 10 Hz of wall time        */

static uint32_t rng_next(void) {
	rng_state = rng_state * 1664525u + 1013904223u;
	return rng_state;
}

static bool midturn_rejected;          /* B22: last ingest held battle state */
static uint16_t roster;                /* owned companies (chest cap 300)  */

static uint8_t taken_count(void) {
	uint8_t n = 0u;
	for (uint8_t m = war_taken(); m != 0u; m >>= 1) {
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
	order_status_hold = 0u;
	deploy_province = -1;
	changed_province = 0xFFu;
	midturn_rejected = false;
	memset(applied_seq, 0, sizeof applied_seq); /* T031: idempotency keyed */
	battle_active = false;
	battle_time = 0u;     /* B22: a resumed run never carries a battle */
	battle_enemies = 0u;
	sealed_valid = false;
	sealed_applied = false;
	rng_state = 1u;
	battle_id_seq = 0u;
	formation_kind = SIM_FORM_LINE;
	memset(&bx, 0, sizeof bx);
	memset(&field, 0, sizeof field);
	memset(field_views, 0, sizeof field_views);
}

/* ---- lifecycle ---------------------------------------------------------- */

void sim_init(uint32_t seed) {
	reset_run_state();
	rng_state = seed ? seed : 1u;
	boot_seed = rng_state;
	war_reset(boot_seed);
	sim_autoform();
}

void sim_ingest_save(const save_aggregate_t *agg, uint16_t day) {
	reset_run_state();
	if (agg == NULL) {
		war_reset(boot_seed);
		sim_autoform();
		return;
	}
	/* B25 range clamps (spec-06 T012): a CRC-valid but hostile or
	 * stale record can never push the run past its caps - treasury,
	 * roster, chest and the muster queue are clamped, never trusted.  */
	snap.treasury = (agg->treasury > SIM_TREASURY_CAP) ? SIM_TREASURY_CAP
	                                                   : agg->treasury;
	snap.day = day ? day : 1u;
	/* B13 selection restore: out-of-range byte -> row 0, never a
	 * VERIFY_FAIL (spec-02 T033).                                     */
	snap.selected = (agg->selected_province < (uint8_t)FRONT_COUNT)
	                    ? agg->selected_province
	                    : 0u;
	rng_state = (agg->rng_state != 0u) ? agg->rng_state : 1u;
	boot_seed = (agg->seed != 0u) ? agg->seed : 1u;
	roster = (agg->unit_count > SIM_CHEST_CAP) ? (uint16_t)SIM_CHEST_CAP
	                                          : agg->unit_count;
	/* the campaign map: held / lost / threatened fronts, relics, wins  */
	war_ingest(agg, boot_seed);
	/* the formation kind rides the v1 `formation` byte (0 = Line on
	 * every record written before it existed); a hostile byte clamps. */
	formation_kind = (agg->formation < SIM_FORM_COUNT) ? agg->formation
	                                                   : SIM_FORM_LINE;
	chest = (uint16_t)agg->reserved[0] | ((uint16_t)agg->reserved[1] << 8);
	if (chest > SIM_CHEST_CAP) {
		chest = SIM_CHEST_CAP;
	}
	if (chest < roster) {
		chest = roster; /* the chest counts every company ever bought */
	}
	muster_pending = (agg->pending_reward > SIM_PENDING_CAP)
	                     ? (uint8_t)SIM_PENDING_CAP
	                     : agg->pending_reward;
	if ((uint16_t)muster_pending + roster > SIM_CHEST_CAP) {
		muster_pending = (uint8_t)(SIM_CHEST_CAP - roster);
	}
	/* B22 (spec-04 T029): a battle never resumes mid-fight. A record
	 * carrying the in-battle marker is refused (the run restarts at the
	 * campaign table, the next deploy starts at tick 0).               */
	midturn_rejected = (agg->reserved[3] != 0u);
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
	out->formation = formation_kind;
	out->unit_count = (uint16_t)roster;
	out->unit_type_aggregates[0] = (uint16_t)roster;
	out->pending_reward = muster_pending;
	out->selected_province = (snap.selected < (uint8_t)FRONT_COUNT)
	                             ? snap.selected
	                             : 0u;
	out->reserved[0] = (uint8_t)(chest & 0xFFu);
	out->reserved[1] = (uint8_t)((chest >> 8u) & 0xFFu);
	out->reserved[2] = muster_active_batch ? 1u : 0u;
	out->reserved[3] = 0u; /* B22: battle state is never persisted     */
	war_export(out);
}

bool sim_ingest_rejected_midturn(void) {
	return midturn_rejected;
}

uint8_t sim_battle_losses(void) {
	return (uint8_t)(battle_losses > 255u ? 255u : battle_losses);
}

uint8_t sim_battle_enemies_start(void) {
	return (uint8_t)(battle_enemies_open > 255u ? 255u : battle_enemies_open);
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

/* ---- stances + one-tap undo (spec-04 R6-R12, ADR-0008) ------------------ */

static void order_say(const char *text) {
	snprintf(order_status, sizeof order_status, "%s", text);
	order_status_hold = 20u;
}

uint8_t sim_battle_stance(void) {
	return bx.stance;
}

bool sim_undo_armed(void) {
	return bx.undo && battle_active && !sealed_valid;
}

const char *sim_order_status(void) {
	if (order_status_hold == 0u || order_status[0] == '\0') {
		return NULL;
	}
	return order_status;
}

static uint32_t fold(uint32_t h, const void *p, size_t n) {
	const uint8_t *b = (const uint8_t *)p;
	for (size_t i = 0; i < n; ++i) {
		h ^= b[i];
		h *= 0x01000193u;
	}
	return h;
}

uint16_t sim_battle_state_hash(void) {
	uint32_t h = 0x811C9DC5u;
	const uint8_t head[6] = {
	    (uint8_t)(battle_active ? 1u : 0u), battle_province,
	    (uint8_t)(sealed_valid ? 1u : 0u), (uint8_t)(sealed_applied ? 1u : 0u),
	    bx.stance, bx.flank,
	};
	h = fold(h, head, sizeof head);
	h = fold(h, &battle_time, sizeof battle_time);
	h = fold(h, &battle_enemies, sizeof battle_enemies);
	h = fold(h, &roster, sizeof roster);
	h = fold(h, &rng_state, sizeof rng_state);
	h = fold(h, &bx.morale, sizeof bx.morale);
	h = fold(h, &bx.foe_morale, sizeof bx.foe_morale);
	h = fold(h, &bx.cooldown, sizeof bx.cooldown);
	h = fold(h, &bx.next_strike, sizeof bx.next_strike);
	h = fold(h, field_views, sizeof field_views);
	return (uint16_t)((h >> 16) ^ (h & 0xFFFFu));
}

const sim_division_t *sim_battle_divisions(uint8_t side, uint8_t *count) {
	side &= 1u;
	if (count != NULL) {
		*count = (battle_active || sealed_valid) ? field.n[side] : 0u;
	}
	return field_views[side];
}

/* ---- battle depth: data, copy and the general (GAME_DESIGN 7-8) --------- */

static const char *const FORM_NAMES[SIM_FORM_COUNT] = {
	"Line", "Phalanx", "Wedge", "Circle",
};
static const char *const FORM_BRIEFS[SIM_FORM_COUNT] = {
	"Every lane held. No edge.",
	"Shield wall ahead. Bare sides.",
	"Spearhead: shock on contact.",
	"No sides to turn. Slow, soft.",
};
static const char *const TERRAIN_NAMES[SIM_TERRAIN_COUNT] = {
	"Plains", "Desert", "Highlands", "Arena",
};
static const char *const TERRAIN_BRIEFS[SIM_TERRAIN_COUNT] = {
	"Open ground: riders flank.",
	"Heat: morale drains.",
	"High ground: Hold hits +25%.",
	"Divine ground: the god rules.",
};
static const char *const BOSS_NAMES[SIM_BOSSES] = {
	"Zeus", "Poseidon", "Hades", "Ares", "Kronos",
};
/* the omen names the counter 6 s before the god acts (GDD 8: the
 * player must have time to understand the threat)                       */
static const char *const BOSS_OMENS[SIM_BOSSES] = {
	"Storm gathers: Fall back!",
	"The tide stirs: Hold!",
	"The dead stir: Charge!",
	"Ares stirs fury: Rally!",
	"Time bends: Flank!",
};
static const char *const BOSS_STRIKES[SIM_BOSSES] = {
	"Thunderbolt! Companies fall.",
	"Flood! Our blows weaken.",
	"The dead rise: foes return.",
	"Ares' fury: blood both ways.",
	"Kronos steals 30 seconds!",
};
static const char *const RELIC_NAMES[SIM_BOSSES] = {
	"Keraunos", "Trident", "Helm", "Spear", "Harpe",
};
static const char *const RELIC_BRIEFS[SIM_BOSSES] = {
	"First 20 s: blows +25%.",
	"Flank stance: blows +25%.",
	"First loss each battle spared.",
	"Morale starts at 75.",
	"Battles last 60 s longer.",
};
static const char *const ABILITY_NAMES[SIM_ABILITIES] = {"Rally", "Charge"};

#define RELIC_BIT(b) ((uint8_t)(1u << (b)))

uint8_t sim_formation_kind(void) {
	return formation_kind;
}

const char *sim_formation_name(uint8_t kind) {
	return FORM_NAMES[kind < SIM_FORM_COUNT ? kind : 0u];
}

const char *sim_formation_brief(uint8_t kind) {
	return FORM_BRIEFS[kind < SIM_FORM_COUNT ? kind : 0u];
}

uint8_t sim_front_terrain(uint8_t province) {
	return FRONT_TERRAIN[province & 7u];
}

const char *sim_terrain_name(uint8_t terrain) {
	return TERRAIN_NAMES[terrain < SIM_TERRAIN_COUNT ? terrain : 0u];
}

const char *sim_terrain_brief(uint8_t terrain) {
	return TERRAIN_BRIEFS[terrain < SIM_TERRAIN_COUNT ? terrain : 0u];
}

uint8_t sim_front_boss(uint8_t province) {
	return FRONT_BOSS[province & 7u];
}

const char *sim_boss_name(uint8_t boss) {
	return (boss < SIM_BOSSES) ? BOSS_NAMES[boss] : "";
}

const char *sim_boss_omen(uint8_t boss) {
	return (boss < SIM_BOSSES) ? BOSS_OMENS[boss] : "";
}

const char *sim_relic_name(uint8_t boss) {
	return (boss < SIM_BOSSES) ? RELIC_NAMES[boss] : "";
}

const char *sim_relic_brief(uint8_t boss) {
	return (boss < SIM_BOSSES) ? RELIC_BRIEFS[boss] : "";
}

static uint8_t morale_of(int32_t q) {
	const int32_t m = (q + MQ - 1) / MQ;
	return (uint8_t)(m < 0 ? 0 : m > (int32_t)SIM_MORALE_MAX ? (int32_t)SIM_MORALE_MAX : m);
}

uint8_t sim_battle_morale(void) {
	return morale_of(bx.morale);
}

uint8_t sim_battle_foe_morale(void) {
	return morale_of(bx.foe_morale);
}

uint8_t sim_battle_flank(void) {
	return bx.flank;
}

uint8_t sim_battle_boss_phase(void) {
	return bx.boss_phase;
}

uint8_t sim_battle_boss(void) {
	return (battle_active || sealed_valid) ? battle_setup.boss : (uint8_t)SIM_BOSS_NONE;
}

uint8_t sim_battle_kind(void) {
	return battle_setup.kind;
}

uint8_t sim_battle_boss_eta(void) {
	if (!battle_active || battle_setup.boss == SIM_BOSS_NONE ||
	    bx.next_strike <= battle_time) {
		return 0u;
	}
	const unsigned s = (bx.next_strike - battle_time + 9u) / 10u;
	return (uint8_t)(s > 255u ? 255u : s);
}

uint16_t sim_battle_time_cap(void) {
	if (battle_active || sealed_valid) {
		return bx.time_cap;
	}
	return (uint16_t)(SIM_BATTLE_TIME_CAP +
	                  ((sim_relic_mask() & RELIC_BIT(SIM_BOSS_KRONOS)) ? SIM_BATTLE_TIME_HARPE
	                                                                  : 0u));
}

uint8_t sim_ability_cooldown(uint8_t ability) {
	if (ability >= SIM_ABILITIES) {
		return 0xFFu;
	}
	return (uint8_t)((bx.cooldown[ability] + 9u) / 10u);
}

bool sim_charge_armed(void) {
	return bx.charge > 0u;
}

uint8_t sim_battle_event(void) {
	return bx.event;
}

uint16_t sim_battle_event_seq(void) {
	return event_seq;
}

const char *sim_battle_event_copy(void) {
	static char copy[40];
	const uint8_t boss = battle_setup.boss;
	switch (bx.event) {
	case SIM_EVT_CLASH:        return "The lines clash!";
	case SIM_EVT_FLANK_HELD:   return "Flank held: the ring stands.";
	case SIM_EVT_WAVERING:     return "Morale wavers. Rally or Hold.";
	case SIM_EVT_FOE_ROUT:     return "The enemy line breaks!";
	case SIM_EVT_FLANK_BROKEN: return "Our flank is broken!";
	case SIM_EVT_CAV_CHARGE:   return "Enemy riders swing wide!";
	case SIM_EVT_AMBUSH:       return "Ambush! The march is struck.";
	case SIM_EVT_BOSS_OMEN:    return sim_boss_omen(boss);
	case SIM_EVT_BOSS_COUNTERED:
		snprintf(copy, sizeof copy, "%s's blow is countered!",
		         sim_boss_name(boss));
		return copy;
	case SIM_EVT_BOSS_STRIKE:
		return (boss < SIM_BOSSES) ? BOSS_STRIKES[boss] : "";
	case SIM_EVT_BOSS_PHASE:
		snprintf(copy, sizeof copy, "%s enrages!", sim_boss_name(boss));
		return copy;
	case SIM_EVT_COLLAPSE:     return "A division broke and fled!";
	case SIM_EVT_RALLY:        return "The general rallies the line.";
	case SIM_EVT_CHARGE:       return "The general leads the charge!";
	default:                   return NULL;
	}
}

static void post_event(uint8_t evt) {
	bx.event = evt;
	event_seq++;
}

static void refresh_views(void) {
	field_view(&field, SIM_SIDE_OURS, field_views[0]);
	field_view(&field, SIM_SIDE_FOE, field_views[1]);
}

/* battle_open(): the field every fresh attempt starts from (deploy and
 * Retry share it, so attempt 2 == attempt 1 holds for every rule).      */
static void battle_open(void) {
	const uint8_t boss = battle_setup.boss;
	const uint8_t relics = sim_relic_mask();
	memset(&bx, 0, sizeof bx);
	bx.morale = ((relics & RELIC_BIT(SIM_BOSS_ARES)) ? 75 : 60) * MQ;
	bx.foe_morale = (boss != SIM_BOSS_NONE)
	                    ? (int32_t)SIM_MORALE_MAX * MQ
	                    : (int32_t)(50u + 10u * provinces[battle_province].threat) * MQ;
	bx.boss_phase = (boss != SIM_BOSS_NONE) ? 1u : 0u;
	bx.next_strike = (boss != SIM_BOSS_NONE) ? GOD_FIRST : 0u;
	bx.time_cap = (uint16_t)((boss != SIM_BOSS_NONE ? DIVINE_CAP : SIM_BATTLE_TIME_CAP) +
	                         ((relics & RELIC_BIT(SIM_BOSS_KRONOS)) ? SIM_BATTLE_TIME_HARPE : 0u));
	bx.relic_guard = (relics & RELIC_BIT(SIM_BOSS_HADES)) != 0u;
	bx.stance = SIM_ORDER_ADVANCE;
	bx.prev_stance = SIM_ORDER_ADVANCE;
	battle_time = 0u;
	field_open(&field, roster, formation_kind, battle_enemies_open, battle_setup.terrain,
	           battle_seed);
	if (battle_setup.ambush) {
		/* struck on the march: the foe is upon the column, the wings
		 * are already turned and the ranks shaken                     */
		for (uint8_t i = 0; i < field.n[1]; ++i) {
			field.d[1][i].x -= 96 * FIELD_SUB;
		}
		bx.flank = SIM_FLANK_BROKEN;
		bx.morale -= 15 * MQ;
	}
	battle_enemies = field_companies(&field, SIM_SIDE_FOE);
	order_status_hold = 0u;
	refresh_views();
}

/* ability_use(): the general acts at once (GDD 7: no micro of soldiers,
 * limited by cooldowns); it is not a stance, so Undo never refunds it.  */
static sim_result_t ability_use(uint8_t ability) {
	if (!battle_active || sealed_valid) {
		order_say("Orders need a live battle.");
		return SIM_BAD_ARG;
	}
	if (ability >= SIM_ABILITIES) {
		return SIM_BAD_ARG;
	}
	if (bx.cooldown[ability] > 0u) {
		snprintf(order_status, sizeof order_status, "%s ready in %u s.",
		         ABILITY_NAMES[ability], (unsigned)sim_ability_cooldown(ability));
		order_status_hold = 20u;
		return SIM_BAD_ARG;
	}
	if (ability == SIM_ABILITY_RALLY) {
		bx.morale += 35 * MQ;
		if (bx.morale > (int32_t)SIM_MORALE_MAX * MQ) {
			bx.morale = (int32_t)SIM_MORALE_MAX * MQ;
		}
		bx.flank = SIM_FLANK_SECURE; /* the general re-forms the wings */
		bx.flank_quiet = 0u;
		bx.rally = SIM_RALLY_TICKS;
		bx.cooldown[ability] = SIM_RALLY_COOLDOWN * SIM_BATTLE_HZ;
		post_event(SIM_EVT_RALLY);
		order_say("Rally! Morale +35.");
	} else {
		bx.charge = SIM_CHARGE_TICKS;
		bx.cooldown[ability] = SIM_CHARGE_COOLDOWN * SIM_BATTLE_HZ;
		post_event(SIM_EVT_CHARGE);
		order_say("Charge! Blows +60% for 6 s.");
	}
	return SIM_OK;
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
	case 0: { /* march on a front: debit + Base preset (spec-02 R4/R6) */
		if (cmd->arg >= (uint8_t)FRONT_COUNT) {
			return SIM_BAD_ARG;
		}
		if (sim_front_action(cmd->arg) == SIM_ACT_HELD) {
			return SIM_BAD_ARG; /* held and quiet: nothing to fight  */
		}
		/* defending your own land is free; conquest is paid up front */
		const uint16_t cost = sim_front_cost(cmd->arg);
		if (snap.treasury < cost) {
			return SIM_INSUFFICIENT_TREASURY; /* R6 dispatch re-check */
		}
		snap.treasury -= cost;
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
		/* Encounter seed derives from persisted identity, never from
		 * the rolling LCG state - this is what makes attempt 2 ==
		 * attempt 1 frame-for-frame (spec-05 R6, ADR-0007).         */
		const uint32_t seed = boot_seed * 2654435761u ^
		                      (uint32_t)(province + 1u) * 97654321u ^
		                      (uint32_t)roster ^ (uint32_t)snap.day;
		war_battle_t wb;
		if (!war_battle(province, roster, seed, &wb)) {
			return SIM_BAD_ARG; /* held and quiet: nothing to fight  */
		}
		battle_setup = wb;
		battle_active = true;
		battle_province = province;
		battle_seed = seed;
		rng_state = battle_seed;
		sealed_valid = false; /* a fresh fight owns its own outcome  */
		battle_enemies_open = wb.foes;
		battle_roster_start =
		    (uint8_t)(roster > 255u ? 255u : roster);
		battle_roster_deploy = roster;
		battle_losses = 0u;
		sealed_applied = false;
		battle_open();
		++battle_id_seq;
		return SIM_OK;
	}
	case 5: /* Continue: apply the sealed outcome exactly once          */
		return sim_result_apply();
	case 6: /* Retry: pre-battle state, same seed (spec-05 R6)          */
		return sim_result_retry();
	case 7: { /* stance: arg 0..3, live from the next tick (ADR-0008)    */
		if (!battle_active || sealed_valid) {
			order_say("Orders need a live battle.");
			return SIM_BAD_ARG;
		}
		if (cmd->arg > SIM_ORDER_FALLBACK) {
			return SIM_BAD_ARG;
		}
		if (cmd->arg != bx.stance) {
			bx.prev_stance = bx.stance;
			bx.stance = cmd->arg;
			bx.undo = true;
		}
		snprintf(order_status, sizeof order_status, "Stance: %s.",
		         ORDER_NAMES[cmd->arg]);
		order_status_hold = 20u;
		return SIM_OK;
	}
	case 9: { /* one-tap undo: the previous stance back (single level)  */
		if (!sim_undo_armed()) {
			return SIM_BAD_ARG; /* chip shows UNDO off            */
		}
		bx.stance = bx.prev_stance;
		bx.undo = false;
		snprintf(order_status, sizeof order_status, "Undo: %s again.",
		         ORDER_NAMES[bx.stance]);
		order_status_hold = 20u;
		return SIM_OK;
	}
	case 10: /* the general's ability: arg = SIM_ABILITY_* (GDD 7)      */
		return ability_use(cmd->arg);
	case 11: /* formation kind for the next deploy (GDD 7, Base tab)    */
		if (cmd->arg >= SIM_FORM_COUNT || battle_active) {
			return SIM_BAD_ARG; /* the line forms before the march */
		}
		if (formation_kind != cmd->arg) {
			formation_kind = cmd->arg;
			save_set_dirty();
		}
		return SIM_OK;
	default:
		return SIM_BAD_ARG; /* 8 (End turn) retired with ADR-0008      */
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
			/* ADR-0009: the campaign's threat clock runs on days */
			const uint32_t before = snap.treasury;
			war_new_day(snap.day, battle_active, &snap.treasury);
			if (snap.treasury != before) {
				save_set_dirty(); /* a raid or a sack is a debit */
			}
			ops_counter += 2u; /* day + treasury updates */
		}
		ops_last_step = ops_counter;
	}

	if ((run_modes & SIM_RUN_MUSTER) != 0u) {
		muster_step((uint32_t)ticks);
	}
	/* the order copy's 2 s hold ticks with the frame clock - wall time,
	 * not field time, so x4 does not shorten it (T024).                */
	if (order_status_hold > 0u) {
		order_status_hold--;
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

/* ---- battle (spec-04 R1-R13, real time per ADR-0008) -------------------- */

bool sim_battle_active(void) {
	return battle_active;
}

bool sim_battle_sealed(void) {
	return sealed_valid;
}

bool sim_outcome_applied(void) {
	return sealed_valid && sealed_applied;
}

uint16_t sim_battle_time(void) {
	return battle_time;
}

uint8_t sim_battle_enemies(void) {
	return (uint8_t)(battle_enemies > 255u ? 255u : battle_enemies);
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

/* the tick's alert: SIM_EVT_* values are ordered by priority, so the
 * most critical thing that happened this tick is the one shown         */
#define OFFER(e) do { if ((uint8_t)(e) > evt) { evt = (uint8_t)(e); } } while (0)

/* companies of ours that fell: the roster, the ledger and morale       */
static void take_losses(uint16_t n) {
	if (n > 0u && bx.relic_guard) {
		/* the helm of Hades: the first company to fall gets back up   */
		bx.relic_guard = false;
		if (field_raise(&field, SIM_SIDE_OURS, 1u) == 1u) {
			n--;
		}
	}
	for (uint16_t i = 0; i < n && roster > 0u; ++i) {
		roster--;
		battle_losses++;
		ops_counter++;
	}
	/* losing half the army costs ~30 morale, whatever its size       */
	bx.morale -= (int32_t)n * 60 * MQ / (battle_roster_start ? battle_roster_start : 1);
}

/* the god acts on its own clock, telegraphed GOD_OMEN ticks early; the
 * stance or the general's act at the instant of the blow is the answer */
static uint8_t god_tick(uint8_t evt) {
	const uint8_t boss = battle_setup.boss;
	if (boss == SIM_BOSS_NONE) {
		return evt;
	}
	if (battle_time + GOD_OMEN == bx.next_strike) {
		OFFER(SIM_EVT_BOSS_OMEN);
	}
	if (battle_time != bx.next_strike) {
		return evt;
	}
	/* a blow's weight scales with both hosts, so a god stays a god at
	 * any army size (GAME_DESIGN 8: progression helps, never trivialises) */
	const uint16_t unit = (uint16_t)(battle_roster_start / 10u ? battle_roster_start / 10u : 1u);
	const uint16_t fu = (uint16_t)(battle_enemies_open / 10u ? battle_enemies_open / 10u : 1u);
	const uint16_t w = bx.wrath;
	bool countered = false;
	switch (boss) {
	case SIM_BOSS_ZEUS:
		countered = bx.stance == SIM_ORDER_FALLBACK; /* scattered: no mark */
		if (!countered) {
			take_losses(field_strike(&field, SIM_SIDE_OURS, (uint16_t)((2u + w) * unit)));
			bx.morale -= 10 * MQ;
		}
		break;
	case SIM_BOSS_POSEIDON:
		countered = bx.stance == SIM_ORDER_HOLD; /* locked shields ride it */
		if (!countered) {
			bx.flood = (uint16_t)(200u + 100u * w);
			take_losses(field_strike(&field, SIM_SIDE_OURS, (uint16_t)((1u + w) * unit)));
			bx.morale -= 8 * MQ;
		}
		break;
	case SIM_BOSS_HADES:
		countered = bx.charge > 0u; /* the charge tramples the risen    */
		if (!countered) {
			field_raise(&field, SIM_SIDE_FOE, (uint16_t)((3u + w) * fu));
			bx.morale -= 6 * MQ;
		}
		break;
	case SIM_BOSS_ARES:
		/* a calm line over his fury: the rally, or nerve to spare   */
		countered = bx.rally > 0u || sim_battle_morale() >= 80u;
		if (!countered) {
			take_losses(field_strike(&field, SIM_SIDE_OURS, (uint16_t)((2u + w) * unit)));
			bx.morale -= 12 * MQ;
		}
		break;
	default: /* SIM_BOSS_KRONOS                                        */
		countered = bx.stance == SIM_ORDER_FLANK;
		if (!countered) {
			/* the stolen time, and the wounds it rewinds              */
			bx.time_cap = (bx.time_cap > battle_time + 600u) ? (uint16_t)(bx.time_cap - 300u)
			                                                : (uint16_t)(battle_time + 300u);
			field_raise(&field, SIM_SIDE_FOE, (uint16_t)((2u + w) * fu));
		}
		break;
	}
	if (countered) {
		/* an answered blow turns on the god's own host               */
		field_strike(&field, SIM_SIDE_FOE, fu);
		bx.morale += 4 * MQ;
	} else if (bx.wrath < 3u) {
		bx.wrath++; /* a god ignored grows; a god answered does not     */
	}
	bx.next_strike = (uint16_t)(battle_time + (bx.boss_phase >= 2u ? GOD_RAGE : GOD_EVERY));
	OFFER(countered ? SIM_EVT_BOSS_COUNTERED : SIM_EVT_BOSS_STRIKE);
	return evt;
}

static void seal(bool victory) {
	/* R11: deterministic seal in 1 tick; no save write here.           */
	battle_active = false;
	sealed.version = 1u;
	sealed.result = victory ? 1u : 0u;
	sealed.province_id = battle_province;
	sealed.province_state = victory ? 1u : 0u;
	sealed.battle_id = battle_id_seq;
	sealed.seed = rng_state;
	sealed.treasury_delta = victory ? (int32_t)battle_setup.reward : 0;
	/* B14: the battle hop, trace id = the sealed battle_id.              */
	trace_hop(TRACE_FLOW_BATTLE, 1u, sealed.battle_id);
	/* the days already passed in real time while the field fought      */
	sealed.day_delta = 0u;
	sealed.units_before = battle_roster_start;
	sealed.units_after = (uint8_t)(roster > 255u ? 255u : roster);
	sealed.tier_before = taken_count();
	sealed.tier_after = (uint8_t)(taken_count() +
	                              (victory && !sim_front_taken(battle_province) ? 1u : 0u));
	sealed_valid = true;
	refresh_views();
}

/* sim_battle_step(): one 10 Hz tick of the field. The stance, the
 * formation, the terrain, morale, the general and the god set the
 * field's numbers; field.c moves and strikes; the rules read back what
 * happened. Seals once: the foe destroyed or broken (victory), the army
 * gone or the time run out (defeat).                                    */
void sim_battle_step(void) {
	if (!battle_active || sealed_valid) {
		return;
	}
	battle_time++;
	const uint32_t roll = rng_next(); /* the field's luck this tick     */
	const uint8_t terrain = battle_setup.terrain;
	const uint8_t relics = sim_relic_mask();
	const bool mortal = battle_setup.boss == SIM_BOSS_NONE;
	uint8_t evt = SIM_EVT_NONE;
	if (battle_time == 1u && battle_setup.ambush) {
		OFFER(SIM_EVT_AMBUSH);
	}
	for (int a = 0; a < (int)SIM_ABILITIES; ++a) {
		if (bx.cooldown[a] > 0u) {
			bx.cooldown[a]--;
		}
	}
	/* ---- what the field runs on this tick ---------------------------- */
	field_ctl_t ctl;
	memset(&ctl, 0, sizeof ctl);
	ctl.stance = bx.stance;
	ctl.form = formation_kind;
	ctl.charge = bx.charge > 0u;
	ctl.ward = bx.rally > 0u;
	ctl.foe_rout = bx.foe_routing;
	ctl.roll = roll;
	int out = 100, in = 100;
	if (bx.wavering) out -= 20;                      /* shaken arms     */
	if (ctl.charge) out += 60;
	if (bx.flood > 0u) out -= 30;
	if (battle_time <= 200u && (relics & RELIC_BIT(SIM_BOSS_ZEUS))) out += 25;
	if (bx.stance == SIM_ORDER_FLANK && (relics & RELIC_BIT(SIM_BOSS_POSEIDON))) out += 25;
	if (bx.stance == SIM_ORDER_HOLD && terrain == SIM_TERRAIN_HIGHLANDS) out += 25;
	if (formation_kind == SIM_FORM_CIRCLE) out -= 10;
	if (bx.stance == SIM_ORDER_HOLD) in = 75 * battle_setup.hold_in_pct / 100;
	if (bx.stance == SIM_ORDER_FALLBACK) in = 50;
	if (formation_kind == SIM_FORM_WEDGE) in += 15;  /* the point bleeds */
	ctl.out_pct[0] = (uint16_t)out;
	ctl.in_pct[0] = (uint16_t)in;
	ctl.out_pct[1] = 100u;
	ctl.in_pct[1] = bx.foe_routing ? 150u : 100u;    /* backs turned    */
	if (bx.charge > 0u) bx.charge--;
	if (bx.rally > 0u) bx.rally--;
	if (bx.flood > 0u) bx.flood--;

	field_report_t rep;
	field_tick(&field, &ctl, &rep);
	ops_counter += field_ops();

	/* ---- what the field did ------------------------------------------ */
	take_losses(rep.lost[0]);
	if (rep.lost[1] > 0u) {
		const int32_t open = battle_enemies_open ? battle_enemies_open : 1;
		bx.morale += (int32_t)rep.lost[1] * 30 * MQ / open;
		if (mortal) {
			bx.foe_morale -= (int32_t)rep.lost[1] * 80 * MQ / open;
		}
	}
	if (rep.clash) {
		OFFER(SIM_EVT_CLASH);
	}
	/* flanks: a side blow breaks them, riders swinging wide press them,
	 * quiet lets them re-form one step every 5 s                        */
	if (rep.flanked[0] > 0u) {
		if (bx.flank != SIM_FLANK_BROKEN) {
			bx.flank = SIM_FLANK_BROKEN;
			bx.morale -= 6 * MQ;
			OFFER(SIM_EVT_FLANK_BROKEN);
		}
		if (battle_time % 30u == 0u) {
			bx.morale -= MQ; /* a turned line keeps bleeding nerve      */
		}
		bx.flank_quiet = 0u;
	} else if (rep.blunted > 0u) {
		if (!bx.held) { /* one alert per side charge the ring takes */
			bx.held = true;
			bx.flank = SIM_FLANK_PRESSED;
			bx.morale += 3 * MQ;
			OFFER(SIM_EVT_FLANK_HELD);
		}
		bx.flank_quiet = 0u;
	} else if (rep.wheeling > 0u) {
		if (bx.flank == SIM_FLANK_SECURE) {
			bx.flank = SIM_FLANK_PRESSED;
		}
		OFFER(SIM_EVT_CAV_CHARGE);
		bx.flank_quiet = 0u;
	} else if (++bx.flank_quiet >= 50u) {
		bx.flank_quiet = 0u;
		bx.held = false; /* 5 s quiet: the next side charge is news   */
		if (bx.flank > SIM_FLANK_SECURE) {
			bx.flank--;
		}
	}
	if (rep.flanked[1] > 0u && mortal && battle_time % 30u == 0u) {
		bx.foe_morale -= MQ; /* their turned line loses heart       */
	}
	evt = god_tick(evt);
	/* the slow pulses: a held line steadies, the ring calms, heat drains */
	if (bx.stance == SIM_ORDER_HOLD && battle_time % 20u == 0u) bx.morale += MQ;
	if (formation_kind == SIM_FORM_CIRCLE && battle_time % 50u == 0u) bx.morale += MQ;
	if (terrain == SIM_TERRAIN_DESERT && battle_time % 30u == 0u) bx.morale -= MQ;
	if (bx.morale > (int32_t)SIM_MORALE_MAX * MQ) bx.morale = (int32_t)SIM_MORALE_MAX * MQ;
	if (bx.foe_morale < 0) bx.foe_morale = 0;
	if (bx.morale <= 0 && field_companies(&field, SIM_SIDE_OURS) > 0u) {
		/* nerve gone: the weakest division breaks and runs           */
		take_losses(field_rout_weakest(&field, SIM_SIDE_OURS));
		bx.morale = 25 * MQ;
		OFFER(SIM_EVT_COLLAPSE);
	}
	if (bx.morale < 0) bx.morale = 0;
	const uint8_t m = sim_battle_morale();
	if (!bx.wavering && m < 30u) {
		bx.wavering = true;
		OFFER(SIM_EVT_WAVERING);
	} else if (bx.wavering && m >= 40u) {
		bx.wavering = false;
	}
	if (mortal && !bx.foe_routing && sim_battle_foe_morale() < 20u) {
		bx.foe_routing = true;
		OFFER(SIM_EVT_FOE_ROUT);
	}
	battle_enemies = field_companies(&field, SIM_SIDE_FOE);
	if (bx.boss_phase == 1u && battle_enemies > 0u &&
	    (uint32_t)battle_enemies * 2u <= battle_enemies_open) {
		bx.boss_phase = 2u; /* enraged: it strikes every 20 s          */
		if (bx.next_strike > battle_time + GOD_RAGE) {
			bx.next_strike = (uint16_t)(battle_time + GOD_RAGE);
		}
		OFFER(SIM_EVT_BOSS_PHASE);
	}
	if (evt != SIM_EVT_NONE) {
		post_event(evt);
	}
	ops_counter += 4u; /* morale + flank + god + seal blocks           */
	ops_last_step = ops_counter;
	refresh_views();
	if (bx.foe_routing) {
		bx.rout_ticks++;
	}
	if (battle_enemies == 0u || bx.rout_ticks >= ROUT_TICKS) {
		seal(true);
	} else if (roster == 0u || field_companies(&field, SIM_SIDE_OURS) == 0u ||
	           battle_time >= bx.time_cap) {
		/* the army is gone, or the day is out and the foe holds the
		 * field: defeat, losses kept visible                          */
		seal(false);
	}
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
	/* ADR-0009: the map takes the verdict - a front held, a threat
	 * answered, a relic won, a battle counted toward the next era      */
	war_settle(sealed.province_id, &battle_setup, sealed.result == 1u);
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
	 * reset the battle counters and hit tick 0 with the same seed.        */
	battle_active = true;
	sealed_valid = false;
	sealed_applied = false;
	/* the companies the first attempt lost come back: Retry is the
	 * pre-battle state, so attempt 2 starts from the same roster.   */
	roster = battle_roster_deploy;
	battle_losses = 0u;
	battle_roster_start =
	    (uint8_t)(roster > 255u ? 255u : roster);
	rng_state = battle_seed; /* attempt 2 == attempt 1 tick-for-tick  */
	battle_open();
	return SIM_OK;
}

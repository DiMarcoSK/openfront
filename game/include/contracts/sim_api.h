/* sim_api.h v1 - the only window ui has into the simulation (constitution 6).
 * Owner: sim module (src/sim). A snapshot is 8 B, a command is 8 B; ui is
 * read-only on the snapshot and writes only through sim_enqueue.
 * Additive only: sim_api_v2.h with an adapter is the breaking-change path.
 */
#ifndef WAR_TABLE_SIM_API_H_V1
#define WAR_TABLE_SIM_API_H_V1

#include <stdint.h>
#include <stdbool.h>
#include "save.h"      /* the 63 B commit payload (ADR-0002, same ABI). */

#define SIM_ABI_VERSION 1u

#pragma pack(push, 1)

/* The snapshot ui reads once per frame. 8 B. */
typedef struct sim_snapshot {
	uint32_t treasury;
	uint16_t day;
	uint8_t  selected;   /* province 0..7, never a pointer (spec-02 R1) */
	uint8_t  flags;      /* bit0 dirty, bit1 queue_full                 */
} sim_snapshot_t;

/* Command envelope: 8 B. Capacity 32, idempotency key = seq.             */
typedef struct sim_cmd {
	uint32_t seq;
	uint8_t  cmd;        /* 0 = recruit, 1 = formation swap, 2 = tier   */
	uint8_t  arg;        /* province id or slot pair                     */
	uint16_t pad;
} sim_cmd_t;

/* Result codes from sim_enqueue / sim application.                       */
typedef enum sim_result {
	SIM_OK = 0,
	SIM_INSUFFICIENT_TREASURY,
	SIM_QUEUE_FULL,
	SIM_CHEST_FULL,      /* additive B09: chest 298/300 cap inline      */
	SIM_BAD_ARG,
	SIM_DUPLICATE,       /* additive B12: replayed seq short-circuits   */
} sim_result_t;

/* ---- battle outcome (spec-05 plan section 2, pinned by ADR-0002) ------- */

typedef struct battle_outcome {
	uint8_t version;
	uint32_t battle_id;
	uint8_t result;         /* 0 = defeat, 1 = victory                   */
	uint32_t seed;
	int32_t treasury_delta;
	uint16_t day_delta;
	uint8_t units_before;
	uint8_t units_after;
	uint8_t province_id;
	uint8_t province_state;
	uint8_t tier_before;
	uint8_t tier_after;
} battle_outcome_t;

_Static_assert(sizeof(battle_outcome_t) == 22, "battle outcome pins to 22 B");

/* Pre-battle snapshot for Retry (spec-05 plan section 2, 129 B cap).     */
typedef struct pre_battle_snapshot {
	uint32_t seed;
	uint32_t battle_id;
	uint8_t attempt;
	uint8_t companies[10][12];
} pre_battle_snapshot_t;

_Static_assert(sizeof(pre_battle_snapshot_t) == 129,
               "pre-battle snapshot pins to 129 B");

/* Sealed once; ui reads the same pointer (spec-05 plan section 3).        */
const battle_outcome_t *sim_seal_outcome(void);

#pragma pack(pop)

_Static_assert(sizeof(sim_snapshot_t) == 8, "sim snapshot must stay 8 B");
_Static_assert(sizeof(sim_cmd_t) == 8, "sim command must stay 8 B");

/* sim_snapshot(): const pointer, valid until the next VBlank frame.       */
const sim_snapshot_t *sim_snapshot(void);

/* sim_enqueue(): false only when the 32-slot ring is full.                */
bool sim_enqueue(const sim_cmd_t *cmd);

/* sim_step(ticks): apply one 10 Hz tick worth of economy + queue work.    */
void sim_step(uint8_t ticks);

/* sim_init(): seed the module state; called once from boot.               */
void sim_init(uint32_t seed);

/* ---- additive B07: boot handoff (spec-01 R2, ADR-0001) ----------------- */

/* sim_ingest_save(): apply a verified aggregate as run state and freeze
 * the tick counter at 0 (the title is a menu; the economy ticks only in
 * Campaign). sim_init() is the New Campaign entry (Day 1 / treasury 300).
 */
void sim_ingest_save(const save_aggregate_t *agg, uint16_t day);

/* sim_tick(): ticks applied since the last ingest (0 on entry).           */
uint32_t sim_tick(void);
uint32_t sim_ops_last_step(void); /* B05 cycle harness: unit ops/frame    */

/* ---- additive B08/B09/B10: the loop economy (spec-02/03/04) ------------
 * Balance constants are data, not magic numbers (spec-02 OQ2, spec-03 OQ1).
 */
#define SIM_COMPANY_COST       12u   /* g per company (spec-03 OQ1)     */
#define SIM_CHEST_CAP          300u  /* lifetime chest (spec-03 R2)     */
#define SIM_PULSE_QTY          5u    /* +5 stepper step (spec-03 R2)    */
#define SIM_MUSTER_TICKS       20u   /* 1 company per 20 ticks (R4)     */
#define SIM_PENDING_CAP        20u   /* muster backpressure (R5)        */
#define SIM_ECON_TICKS_PER_DAY 600u  /* 1 day = 600 ticks (spec-02 OQ2) */
#define SIM_DAY_INCOME         2u    /* +2 treasury per day            */

#define SIM_FRONT_COUNT        8u    /* 8 ROM records (spec-02 OQ1)     */
#define SIM_TREASURY_CAP       9999999u /* B25 ingest clamp (7 digits) */

typedef enum sim_threat {
	SIM_THREAT_LOW = 0,
	SIM_THREAT_MEDIUM,
	SIM_THREAT_HIGH,
	SIM_THREAT_CRITICAL,
} sim_threat_t;

/* One campaign row: name <= 11 chars, static cost/threat/reward
 * (ADR-0005: the front stays a menu row, never map art). 9 B packed.     */
#pragma pack(push, 1)
typedef struct sim_province_record {
	const char *name;
	uint16_t cost;
	uint16_t reward;
	uint8_t threat;
} sim_province_record_t;
#pragma pack(pop)

_Static_assert(sizeof(sim_province_record_t) == sizeof(void *) + 5u,
               "province record pins to 1 ptr + 5 B (packed)");

/* sim_province_table(): the 8 ROM record pointer; *count is the length.  */
const sim_province_record_t *sim_province_table(uint8_t *count);

/* sim_front_taken(): province_state bit of the active aggregate.         */
bool sim_front_taken(uint8_t province);

/* sim_set_run_mode(): loop gates the two 10 Hz consumers per screen:
 * economy (Campaign/Battle) and muster (Campaign/Base/Battle). Menus add
 * 0 ticks of either kind (ADR-0001, spec-02 T022, spec-03 R1).           */
#define SIM_RUN_ECONOMY 0x1u
#define SIM_RUN_MUSTER  0x2u
void sim_set_run_mode(uint8_t modes);

/* sim_apply(): the dispatch path - the conduit hands one envelope to the
 * economy. Re-check at dispatch = one command one debit; a failure
 * returns INSUFFICIENT/QUEUE_FULL/BAD_ARG and the queue logs the reject
 * (spec-02 R6/R7, spec-03 R3). A replayed `seq` short-circuits with
 * SIM_DUPLICATE: 0 debit, no reject-log entry (B12, T031).                */
sim_result_t sim_apply(const sim_cmd_t *cmd);

/* sim_deploy_province(): campaign preset for the Base deploy handoff,
 * -1 when no front is preset (deploy stays blocked inline, spec-03 R10). */
int8_t sim_deploy_province(void);

/* sim_changed_province(): the front the last applied outcome touched
 * (0xFF none) - the Return to Campaign must focus/highlight its row
 * (spec-05 R4).                                                          */
uint8_t sim_changed_province(void);
void sim_clear_changed_province(void);

/* B13 selection (spec-02 T033, additive): the campaign focus travels
 * through the snapshot so the coalesced commit persists the focused row
 * in the v2 byte 62; an out-of-range write is clamped to row 0 there
 * too, never a VERIFY_FAIL.                                              */
uint8_t sim_selected_province(void);
void sim_set_selected(uint8_t province);

/* ---- base slice (spec-03 R2-R8, B09) ----------------------------------- */

#define SIM_FORMATION_SLOTS 10u

uint16_t sim_roster(void);            /* owned companies (chest cap 300)  */
uint16_t sim_chest(void);             /* chest_used 0..300 (12 B records) */
uint8_t  sim_muster_done(void);       /* mustered of the batch           */
uint8_t  sim_muster_total(void);      /* batch size                      */
uint16_t sim_muster_ticks_left(void); /* ticks to the next company       */
uint16_t sim_muster_progress(void);   /* ticks of the running company    */

/* formation[10]: slot -> company id, 0xFF = Empty (spec-03 R6-R8).       */
const uint8_t *sim_formation(void);

/* sim_autoform(): deterministic 1-frame fill of the owned slots.         */
void sim_autoform(void);

/* sim_formation_hash(): FNV-1a over formation[10] (B16, T016): the
 * layout id the golden test re-derives; equal runs -> equal hash, and
 * it is 0 when every slot is Empty.                                      */
uint16_t sim_formation_hash(void);

/* sim_slot_swap(): one tick, roster sum invariant (spec-03 R7).          */
bool sim_slot_swap(uint8_t a, uint8_t b);

/* ---- battle + result (spec-04 R1-R12, spec-05 R1-R9, B10) --------------
 * ADR-0008: the battle runs in REAL TIME (RTS auto-battle, GAME_DESIGN 7):
 * divisions march, wheel, clash and rout on a field; there are no turns.
 * One sim_battle_step() is one 10 Hz tick; x2/x4 run 2/4 ticks per 10 Hz
 * frame with the same rules (the speed never changes the outcome).      */

#define SIM_BATTLE_UNITS 64u  /* animated chips cap (R3)                 */
#define SIM_BATTLE_HZ 10u     /* field ticks per second at x1            */
/* 5 min at x1 (GDD 7: a common battle lasts 2-5 min); the Harpe adds
 * SIM_BATTLE_TIME_HARPE. Reaching the cap seals a defeat (the foe holds
 * the field).                                                          */
#define SIM_BATTLE_TIME_CAP   3000u
#define SIM_BATTLE_TIME_HARPE 600u

/* sim_battle_step(): one 10 Hz field tick while a battle is live. The
 * 300k sim-cycle ceiling is hardware-measured by B05/B19.              */
void sim_battle_step(void);

bool sim_battle_active(void);
uint16_t sim_battle_time(void);       /* ticks fought (0 at deploy)      */
uint16_t sim_battle_time_cap(void);   /* ticks before the field is lost */
uint8_t sim_battle_enemies(void);
uint8_t sim_battle_province(void);
uint8_t sim_battle_units(void);       /* chips on field, cap 64 (R3)     */
uint8_t sim_battle_units_start(void); /* division size at battle start   */

/* sim_battle_sealed(): outcome ready for the Result sheet (<= 1 frame
 * handoff, spec-05 R1); the sealed pointer follows sim_seal_outcome().   */
bool sim_battle_sealed(void);

/* sim_outcome_applied(): true when Continue already applied it (R4);
 * drives the ledger's projected values post-apply.                       */
bool sim_outcome_applied(void);

/* sim_result_apply(): apply the sealed outcome exactly once (R4);
 * second calls are idempotent no-ops with a read of SIM_OK.              */
sim_result_t sim_result_apply(void);

/* sim_result_retry(): restore the pre-battle state and restart the same
 * seed inside <= 3 frames (spec-05 R6, ADR-0007 determinism).            */
sim_result_t sim_result_retry(void);

/* sim_export_aggregate(): full run state -> the 63 B save payload for
 * the coalesced commit (save.c owns all media, spec-06). Chest rides
 * reserved[0], the muster batch reserved[1]; selection byte 62 stays
 * untouched (spec-02 T029).                                              */
void sim_export_aggregate(save_aggregate_t *out);

/* ---- speed x1/x2/x4 (spec-04 R4/R5, B20, T016-T019) -------------------- */

/* 4 sub-steps per frame at x4, same turn rules (spec-04 OQ: T017).
 * The per-unit gate (<= 150 cycles/unit) holds per sub-step; the
 * 300k cycles/frame budget is SHARED across the sub-steps
 * (docs/poc-battle.md), and the runtime clamp below is its ms guard:
 * the sim slice must stay inside 4.0 ms (spec-02 T004).               */
#define SIM_SPEED_X1 1u
#define SIM_SPEED_X2 2u
#define SIM_SPEED_X4 4u
#define SIM_SLICE_US ((uint32_t)4000u) /* the 4.0 ms sim slice       */

/* sim_set_speed(): a player request (x1/x2/x4; anything else reads x1).
 * Applies on the next frame (R4): the ACTIVE rate starts equal to the
 * request and the loop's slice feedback may clamp it down (R5) without
 * touching the request. A fresh request owns a fresh clamp.            */
void sim_set_speed(uint8_t mult);

/* sim_speed(): the ACTIVE rate - the strip badge and the loop
 * sub-stepper BOTH read this, so badge rate == sim rate by identity.   */
uint8_t sim_speed(void);

/* sim_speed_request(): what the player asked for (differs from active
 * only while a clamp is live).                                         */
uint8_t sim_speed_request(void);

/* sim_speed_slice(): one loop feedback per 10 Hz frame with the measured
 * sim slice in MICROSECONDS. Above the 4.0 ms slice the active rate
 * downgrades ONE notch (x4 -> x2 -> x1) and the clamp record is kept so
 * the strip can say what actually runs - no silent slowdown (R5).      */
void sim_speed_slice(uint32_t slice_us);

/* sim_speed_badge(): the strip badge text of the ACTIVE rate
 * ("x1"/"x2"/"x4", R4: badge text equals active rate).                  */
const char *sim_speed_badge(void);

/* sim_speed_clamp_copy(): the R5 copy while a clamp is live, byte-exact
 * per spec-04 states table ("Clamp: \"x4 -> x2: 4.9 ms > 4.0 ms.\""),
 * NULL when the active rate equals the request (no clamp to show).     */
const char *sim_speed_clamp_copy(void);

/* ---- stances + one-tap undo (spec-04 R6-R12 as amended by ADR-0008) ----
 * An order is a STANCE for the whole army, live from the next tick until
 * the next order (cmd 7). No queue, no End turn: the field never waits.
 * Undo (cmd 9) puts the previous stance back (single level).           */

#define SIM_ORDER_ADVANCE  0u /* close and fight                         */
#define SIM_ORDER_HOLD     1u /* stand fast: -25% taken, morale climbs   */
#define SIM_ORDER_FLANK    2u /* wings + riders wheel onto the foe's rear */
#define SIM_ORDER_FALLBACK 3u /* disengage: -50% taken, 0 melee dealt    */

/* sim_battle_state_hash(): FNV-1a fold over the live battle state (time,
 * stance, every division, morale, rng). Same seed + same inputs at the
 * same ticks = same trail (ADR-0007, the B23 golden).                  */
uint16_t sim_battle_state_hash(void);

uint8_t sim_battle_stance(void);      /* SIM_ORDER_* in force            */

/* sim_undo_armed(): a stance change can be taken back (cmd 9).        */
bool sim_undo_armed(void);

/* sim_order_status(): the latest order copy: "Stance: %s." | undo ack |
 * an ability refusal. NULL = nothing to draw; the copy falls away 2 s
 * of wall time after it was set (x4 does not shorten it).               */
const char *sim_order_status(void);

/* ---- the field: divisions for the art + the minimap (ADR-0008) ---------
 * Each side fields up to SIM_DIVISIONS divisions; ours are the formation
 * slots (class by slot), theirs come from the front. x is in map px of
 * the 512 px field (ours start west, facing east). Read-only views. */
#define SIM_DIVISIONS 10u
enum { SIM_SIDE_OURS = 0, SIM_SIDE_FOE };
enum { SIM_CLS_INFANTRY = 0, SIM_CLS_ARCHERS, SIM_CLS_CAVALRY };
enum { SIM_LANE_NORTH = 0, SIM_LANE_CENTRE, SIM_LANE_SOUTH, SIM_LANES };
enum { SIM_DIV_MARCH = 0, SIM_DIV_FIGHT, SIM_DIV_SHOOT, SIM_DIV_WHEEL,
       SIM_DIV_HOLD, SIM_DIV_ROUT, SIM_DIV_DEAD };
#define SIM_DIV_FLANKING 0x1u /* striking a side or the rear            */
#define SIM_DIV_FLANKED  0x2u /* struck in the side or the rear        */

#pragma pack(push, 1)
typedef struct sim_division {
	int16_t x;
	uint8_t lane;
	uint8_t cls;
	uint8_t companies;        /* standing now (cap 255 on the view)   */
	uint8_t companies_start;
	uint8_t state;            /* SIM_DIV_*                             */
	uint8_t flags;            /* SIM_DIV_FLANKING | SIM_DIV_FLANKED    */
} sim_division_t;
#pragma pack(pop)
_Static_assert(sizeof(sim_division_t) == 8, "division view pins to 8 B");

/* sim_battle_divisions(): the side's view array; *count = divisions. */
const sim_division_t *sim_battle_divisions(uint8_t side, uint8_t *count);

/* ---- battle handoff + resume (spec-04 R12, B22, T027-T029) ------------- */

/* sim_ingest_rejected_midturn(): the last ingested aggregate carried the
 * in-battle marker (reserved[3]) and was refused as mid-battle state -
 * the run restarts at the table; a deploy always starts at tick 0.     */
bool sim_ingest_rejected_midturn(void);

/* sim_battle_enemies_start(): the enemy strength the front opened with
 * (6 + 3 x threat) - the art reads it to thin the enemy line.          */
uint8_t sim_battle_enemies_start(void);

/* sim_battle_losses(): companies this battle lost to attrition. The
 * muster keeps running during a battle (spec-03 R4), so units_after -
 * units_before mixes arrivals in; the ledger's "Fallen" reads this.    */
uint8_t sim_battle_losses(void);

/* ---- battle depth (GAME_DESIGN 7-8, additive) ---------------------------
 * Formation kind, terrain, morale, flanks, the general's two abilities,
 * the gods' own rules and their relics. Every rule is O(1) per turn and
 * deterministic per seed; Retry replays it byte-exact (ADR-0007).
 * Commands: 10 = ability (arg SIM_ABILITY_*), 11 = formation kind (arg
 * SIM_FORM_*, refused while a battle is live). The kind persists in the
 * v1 aggregate `formation` byte (0 = Line on older records).            */
enum { SIM_FORM_LINE = 0, SIM_FORM_PHALANX, SIM_FORM_WEDGE, SIM_FORM_CIRCLE,
       SIM_FORM_COUNT };
enum { SIM_TERRAIN_PLAINS = 0, SIM_TERRAIN_DESERT, SIM_TERRAIN_HIGHLANDS,
       SIM_TERRAIN_ARENA, SIM_TERRAIN_COUNT };
/* boss ids follow the RENDER_GOD_* order; a relic bit per boss.          */
enum { SIM_BOSS_ZEUS = 0, SIM_BOSS_POSEIDON, SIM_BOSS_HADES, SIM_BOSS_ARES,
       SIM_BOSS_KRONOS, SIM_BOSSES };
#define SIM_BOSS_NONE 0xFFu
enum { SIM_ABILITY_RALLY = 0, SIM_ABILITY_CHARGE, SIM_ABILITIES };
#define SIM_RALLY_COOLDOWN  30u /* seconds of field time                  */
#define SIM_CHARGE_COOLDOWN 30u
#define SIM_RALLY_TICKS     60u /* the rally holds the wings 6 s          */
#define SIM_CHARGE_TICKS    60u /* the charge drives 6 s                  */
#define SIM_MORALE_MAX      100u
enum { SIM_FLANK_SECURE = 0, SIM_FLANK_PRESSED, SIM_FLANK_BROKEN };
/* alerts, ordered by priority (one per tick, the highest wins)          */
enum { SIM_EVT_NONE = 0, SIM_EVT_CLASH, SIM_EVT_FLANK_HELD,
       SIM_EVT_WAVERING, SIM_EVT_FOE_ROUT, SIM_EVT_FLANK_BROKEN,
       SIM_EVT_CAV_CHARGE, SIM_EVT_AMBUSH, SIM_EVT_BOSS_OMEN,
       SIM_EVT_BOSS_COUNTERED, SIM_EVT_BOSS_STRIKE, SIM_EVT_BOSS_PHASE,
       SIM_EVT_COLLAPSE, SIM_EVT_RALLY, SIM_EVT_CHARGE, SIM_EVT_COUNT };

uint8_t sim_formation_kind(void);
const char *sim_formation_name(uint8_t kind);
const char *sim_formation_brief(uint8_t kind);   /* <= 30 chars          */

uint8_t sim_front_terrain(uint8_t province);
const char *sim_terrain_name(uint8_t terrain);
const char *sim_terrain_brief(uint8_t terrain);
uint8_t sim_front_boss(uint8_t province);        /* SIM_BOSS_NONE if none */
const char *sim_boss_name(uint8_t boss);
const char *sim_boss_omen(uint8_t boss);         /* names the counter     */

/* sim_relic_mask(): bit b = boss b defeated (its front taken).           */
uint8_t sim_relic_mask(void);
const char *sim_relic_name(uint8_t boss);
const char *sim_relic_brief(uint8_t boss);

uint8_t sim_battle_morale(void);                 /* 0..100                */
uint8_t sim_battle_foe_morale(void);
uint8_t sim_battle_flank(void);                  /* SIM_FLANK_*           */
uint8_t sim_battle_boss_phase(void);             /* 0 none, 1, 2          */
uint8_t sim_battle_boss(void);                   /* SIM_BOSS_* or NONE    */
uint8_t sim_ability_cooldown(uint8_t ability);   /* seconds, 0 = ready    */
bool sim_charge_armed(void);                     /* the charge is driving */
/* the god's next blow, in seconds (0 = none due / no god): the strip
 * counts it down so the omen is a plan, not a surprise (GDD 8).       */
uint8_t sim_battle_boss_eta(void);

/* the latest alert: the seq bumps once per new alert so ui shows each
 * exactly once; the copy is NULL for SIM_EVT_NONE.                      */
uint8_t sim_battle_event(void);
uint16_t sim_battle_event_seq(void);
const char *sim_battle_event_copy(void);

/* ---- the endless campaign (GAME_DESIGN 6, ADR-0009) ---------------------
 * The empire never stops being tested: after a calm of a few days a
 * threat lands on a held front (invasion, rebellion, siege, raid, or -
 * every 10 battles won - a god's wrath). Answer it before its days run
 * out or the front falls and has to be retaken. Each 10 battles won is
 * an era: foes grow, plunder grows. All of it rides spare bytes of the
 * v1 aggregate (save-format.md), 0 = an older record's neutral value.  */
enum { SIM_WAR_CONQUEST = 0, SIM_WAR_INVASION, SIM_WAR_REBELLION,
       SIM_WAR_SIEGE, SIM_WAR_RAID, SIM_WAR_WRATH, SIM_WAR_RECONQUEST,
       SIM_WAR_KINDS };
/* what a campaign row offers                                           */
enum { SIM_ACT_ATTACK = 0, SIM_ACT_RETAKE, SIM_ACT_DEFEND, SIM_ACT_HELD };
#define SIM_WAR_NONE 0xFFu

uint8_t sim_war_event(void);         /* SIM_WAR_INVASION.. or NONE        */
uint8_t sim_war_province(void);      /* the threatened front, 0xFF none  */
uint8_t sim_war_days_left(void);     /* days to answer it                */
uint8_t sim_war_calm(void);          /* days until the next threat       */
uint16_t sim_war_seq(void);          /* bumps on every threat and verdict */
const char *sim_war_name(uint8_t kind);
const char *sim_war_copy(void);      /* the live threat, <= 40 chars     */
const char *sim_war_news(void);      /* the last verdict, NULL if none   */

uint8_t sim_era(void);               /* battles won / 10                 */
uint16_t sim_battles_won(void);
bool sim_front_lost(uint8_t province);        /* held once, then lost   */
uint8_t sim_front_action(uint8_t province);   /* SIM_ACT_*              */
uint16_t sim_front_cost(uint8_t province);    /* 0 to defend            */
uint16_t sim_front_reward(uint8_t province);  /* with era + the threat  */
uint8_t sim_battle_kind(void);                /* SIM_WAR_* being fought */

#endif /* WAR_TABLE_SIM_API_H_V1 */

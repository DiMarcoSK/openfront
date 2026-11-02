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
 * economy. Re-cheque at dispatch = one command one debit; a failure
 * returns INSUFFICIENT/QUEUE_FULL/BAD_ARG and the queue logs the reject
 * (spec-02 R6/R7, spec-03 R3).                                            */
sim_result_t sim_apply(const sim_cmd_t *cmd);

/* sim_deploy_province(): campaign preset for the Base deploy handoff,
 * -1 when no front is preset (deploy stays blocked inline, spec-03 R10). */
int8_t sim_deploy_province(void);

/* sim_changed_province(): the front the last applied outcome touched
 * (0xFF none) - the Return to Campaign must focus/highlight its row
 * (spec-05 R4).                                                          */
uint8_t sim_changed_province(void);
void sim_clear_changed_province(void);

/* ---- base slice (spec-03 R2-R8, B09) ----------------------------------- */

#define SIM_FORMATION_SLOTS 10u

uint8_t  sim_roster(void);            /* owned companies                 */
uint16_t sim_chest(void);             /* chest_used 0..300 (12 B records) */
uint8_t  sim_muster_done(void);       /* mustered of the batch           */
uint8_t  sim_muster_total(void);      /* batch size                      */
uint16_t sim_muster_ticks_left(void); /* ticks to the next company       */
uint16_t sim_muster_progress(void);   /* ticks of the running company    */

/* formation[10]: slot -> company id, 0xFF = Empty (spec-03 R6-R8).       */
const uint8_t *sim_formation(void);

/* sim_autoform(): deterministic 1-frame fill of the owned slots.         */
void sim_autoform(void);

/* sim_slot_swap(): one tick, roster sum invariant (spec-03 R7).          */
bool sim_slot_swap(uint8_t a, uint8_t b);

/* ---- battle + result (spec-04 R1-R12, spec-05 R1-R9, B10) -------------- */

#define SIM_BATTLE_TURNS 9u   /* 9 turns cap (spec-04 wireframe T7/9)    */
#define SIM_BATTLE_UNITS 64u  /* animated chips cap (R3)                 */

/* sim_battle_step(): 10 Hz call while a battle is live; ends <= 9 turns
 * later. The 300k sim-cycle ceiling is hardware-measured by B05/B19.     */
void sim_battle_step(void);

bool sim_battle_active(void);
uint8_t sim_battle_turn(void);
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

#endif /* WAR_TABLE_SIM_API_H_V1 */

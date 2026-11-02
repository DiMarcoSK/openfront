/* sim_api.h v1 - the only window ui has into the simulation (constitution 6).
 * Owner: sim module (src/sim). A snapshot is 8 B, a command is 8 B; ui is
 * read-only on the snapshot and writes only through sim_enqueue.
 * Additive only: sim_api_v2.h with an adapter is the breaking-change path.
 */
#ifndef WAR_TABLE_SIM_API_H_V1
#define WAR_TABLE_SIM_API_H_V1

#include <stdint.h>
#include <stdbool.h>

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
 * Campaign). New Campaign passes treasury 300, day 1.                     */
void sim_ingest_save(uint32_t treasury, uint16_t day);

/* sim_tick(): ticks applied since the last ingest (0 on entry).           */
uint32_t sim_tick(void);

#endif /* WAR_TABLE_SIM_API_H_V1 */

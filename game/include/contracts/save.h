/* save.h v1 - save module contract (ADR-0002).
 * Owner: save module (src/save). Everyone else consumes this header only;
 * reading the save module's internals is banned by the include-graph check.
 * Additive only: a breaking change ships save_v2.h with an adapter
 * (constitution 6, spec-06 plan section 2/3).
 */
#ifndef WAR_TABLE_SAVE_H_V1
#define WAR_TABLE_SAVE_H_V1

/* Standard headers are fine; everything else in the module tree is not. */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define SAVE_ABI_VERSION 1u

/* ---- on-disk slot layout (ADR-0002, spec-06 plan section 2) -------------- */
#pragma pack(push, 1)

typedef struct save_header {
	uint8_t magic[4];      /* "TYCN"                                    */
	uint8_t format;        /* 1 = v1; unknown formats refuse to load    */
	uint8_t scenario;      /* reserved                                  */
	uint8_t slot;          /* 0..1 (172 B reserved for a 3rd slot, v2)  */
	uint8_t flags;         /* bit0 = dirty                              */
	uint32_t seq;          /* monotonic per boot; part of idempotency   */
	uint16_t day;          /* resumed day number                        */
	uint16_t payload_len;  /* aggregate bytes actually stored           */
} save_header_t;

typedef struct save_aggregate {
	uint32_t treasury;
	uint32_t tick;
	uint32_t seed;
	uint32_t rng_state;
	uint8_t province_state[6];
	uint8_t formation;
	uint16_t unit_count;
	uint16_t unit_type_aggregates[6];
	uint8_t pending_reward;
	uint8_t reserved[63 - 4 - 4 - 4 - 4 - 6 - 1 - 2 - 12 - 1];
} save_aggregate_t;

typedef struct save_slot {
	save_header_t header;      /* 16 B                                   */
	save_aggregate_t payload;  /* 63 B contents, 150 B capacity          */
	uint8_t spare[150 - sizeof(save_aggregate_t)];
	uint32_t crc32;            /* CRC-32 over header + payload           */
} save_slot_t;

#pragma pack(pop)

_Static_assert(sizeof(save_header_t) == 16, "save header must be 16 B");
_Static_assert(sizeof(save_aggregate_t) == 63, "save aggregate must be 63 B");
_Static_assert(sizeof(save_slot_t) == 170, "save slot must be 170 B");

/* ---- module API ---------------------------------------------------------- */

typedef enum save_status {
	SAVE_STATUS_OK = 0,
	SAVE_STATUS_NO_CARD,
	SAVE_STATUS_VERIFY_FAIL,
	SAVE_STATUS_FULL,
} save_status_t;

typedef enum save_backend_kind {
	SAVE_BACKEND_NONE = 0,
	SAVE_BACKEND_EEPROM,
	SAVE_BACKEND_DLDI,
} save_backend_kind_t;

/* Returns the current status; rendered by ui as a single inline row.      */
save_status_t save_status(void);

/* save_init(): probe the backend, scan both slots, load the newest
 * CRC-verified slot into sim's aggregate. Must run before any draw, so
 * boot honors the 2 s SLO (spec-01 plan section 4).                     */
void save_init(void);

/* save_commit(): write the inactive slot, read back, verify CRC-32,
 * retire only after the verify passes. Capacity 1; a second call in the
 * same frame replaces the pending snapshot (spec-06 plan section 4).    */
save_status_t save_commit(const save_aggregate_t *snap);

/* save_load(): decode the aggregate and return it for sim.                */
save_status_t save_load(save_aggregate_t *out);

/* ---- additive B07: per-slot title cards (spec-01 R1/R5) ----------------
 * Additive to v1 (constitution 6): nothing above changes signed fields or
 * the pinned sizes; the title consumes snapshots only.
 */
#pragma pack(push, 1)
typedef struct save_slot_info {
	uint8_t valid;      /* CRC-verified v1 record present                */
	uint8_t blank;      /* never written yet (no record, not damage)    */
	uint16_t day;       /* header day when valid                        */
	uint32_t treasury;  /* payload treasury when valid                  */
	uint32_t seq;       /* header seq when valid                        */
} save_slot_info_t;
#pragma pack(pop)

_Static_assert(sizeof(save_slot_info_t) == 12, "save slot info pins to 12 B");

/* save_slots(): one descriptor per slot for the title cards, cached from
 * the boot scan. A slot may be valid, blank (first boot) or damaged.      */
void save_slots(save_slot_info_t out[2]);

/* save_load_slot(): decode slot (0..1); returns the module status,
 * VERIFY_FAIL when that slot holds no verified record - Continue on a
 * damaged slot can never load (spec-01 R5).                               */
save_status_t save_load_slot(int slot, save_aggregate_t *out);

/* save_erase(): both slots blank; Erase = Undo 5 s lives in ui (spec-06). */
save_status_t save_erase(void);

/* Human-readable status word, one row, 30 chars per line max (spec-06).   */
const char *save_status_text(save_status_t status);

/* Dirty flag for the coalesced commit (spec-02 T012): a debit sets it,
 * no commit runs on tick or row action.                                  */
void save_set_dirty(void);
bool save_is_dirty(void);

#endif /* WAR_TABLE_SAVE_H_V1 */

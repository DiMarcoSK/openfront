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
	/* B13 (spec-02 plan section 2): byte 62 of the aggregate is the
	 * campaign selection; bytes 38..61 stay reserved. A v1 record's
	 * byte 62 is reserved data, so the v1 reader zero-fills it and the
	 * selection defaults to row 0.                                  */
	uint8_t reserved[63 - 4 - 4 - 4 - 4 - 6 - 1 - 2 - 12 - 1 - 1];
	uint8_t selected_province;
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
	SAVE_STATUS_SAVING,   /* additive B24: a commit is in flight         */
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

/* ---- additive B07: reload day (B08/B10 commit path) --------------------
 * The slot header carries the day; the payload does not (ADR-0002), so
 * the committing frame stamps it next to the 170 B image.                 */
void save_set_day(uint16_t day);

/* Dirty flag for the coalesced commit (spec-02 T012): a debit sets it,
 * no commit runs on tick or row action.                                  */
void save_set_dirty(void);
bool save_is_dirty(void);

/* ---- additive B14: the trace id of the save hop (spec-06 plan SLO) -----
 * The commit seq is the trace id that joins the status row, the slot
 * header and the log line (spec-06 plan: "one number, three hops").      */
uint32_t save_commit_seq(void);

/* ---- additive B13: the v1 -> v2 migration (spec-02 T013) ---------------
 * New commits write format 2 (selection byte 62 honored). A v1 record
 * loads forever with the selection defaulted to row 0 (zero-fill);
 * unknown formats still refuse to load.                                  */
#define SAVE_FORMAT_V1 1u
#define SAVE_FORMAT_V2 2u

/* save_rollback_v1(): the tested rollback writer - encode the v1 subset
 * (format 1, selection byte 62 = 0) into `out[170]` with a recomputed
 * CRC-32 so the rollback image loads in the frozen v1 reader.            */
void save_encode_v1(const save_aggregate_t *snap, uint32_t seq,
                    uint16_t day, uint8_t out[170]);

/* ---- additive B24: inline SaveStatus (spec-06 T018-T023) ---------------
 * One transition table owns every status change; the module and the
 * host test read the same function (spec-06 T018).                       */
typedef enum save_event {
	SAVE_EV_BOOT_NO_HW = 0,   /* probe found no save hardware            */
	SAVE_EV_BOOT_VERIFIED,    /* >= 1 slot passed CRC                    */
	SAVE_EV_BOOT_BLANK,       /* both slots never written                */
	SAVE_EV_BOOT_DAMAGED,     /* bytes present, 0 slots verified         */
	SAVE_EV_COMMIT_BEGIN,
	SAVE_EV_COMMIT_OK,
	SAVE_EV_COMMIT_FAIL,      /* write-and-verify failed twice           */
	SAVE_EV_COMMIT_NO_HW,
	SAVE_EV_ERASE,
	SAVE_EV_UNDO,
	SAVE_EV_COUNT,
} save_event_t;

save_status_t save_status_next(save_status_t cur, save_event_t ev);

#define SAVE_COMMIT_ATTEMPTS 2u   /* R7: 2 attempts then FULL            */
#define SAVE_CORE_BYTES 340u      /* 2 x 170 B slots, the Erase hold     */
#define SAVE_UNDO_FRAMES 300u     /* "Undo 5 s" at 60 Hz                 */

/* The last commit as the status row reads it (seq + slot + day +
 * treasury + attempts of the latest try; ok = it verified).              */
typedef struct save_commit_info {
	uint32_t seq;
	uint32_t treasury;
	uint16_t day;
	uint8_t slot;        /* 0..1, 0xFF before the first commit          */
	uint8_t attempts;    /* writes spent by the latest try (0..2)       */
	uint8_t ok;
	uint8_t tried;       /* a commit ran in this boot                   */
} save_commit_info_t;

void save_last_commit(save_commit_info_t *out);

/* save_retry(): the Settings "Retry save" - one fresh try (2 attempts)
 * of the last staged aggregate; the only way out of FULL (R7).           */
save_status_t save_retry(void);

/* Erase = Undo 5 s: save_erase() holds both 340 B images in RAM and
 * blanks the slots; save_erase_undo() writes them back while the hold
 * lives; save_erase_settle() drops the hold when the 5 s run out.        */
bool save_erase_undo(void);
void save_erase_settle(void);
bool save_erase_pending(void);

#endif /* WAR_TABLE_SAVE_H_V1 */

/* save.c - the save module implementation (B06, spec-06 R1-R12).
 * Commit protocol: pick the active slot, write the inactive slot, read
 * back, verify CRC-32, retire only after the verify passes. Boot scan
 * picks the highest valid seq; a cardless card never blocks play.
 */
#include <nds.h>
#include <calico.h>
#include <stdio.h>

#include "contracts/save.h"
#include "save_backend.h"
#include "slots.h"

static save_status_t current_status = SAVE_STATUS_NO_CARD;
static uint8_t block[512];
static int active_slot = -1;
static uint32_t commit_seq;
static bool dirty;
static uint16_t pending_day;

/* The snapshot sim hands us; save owns no economy state (constitution 6). */
static save_aggregate_t restored;

void save_init(void) {
	backend_probe();
	slot_boot_scan(block, &active_slot, &restored, &current_status);
	commit_seq = 0u;
	dirty = false;
}

save_status_t save_status(void) {
	return current_status;
}

void save_set_dirty(void) {
	dirty = true;
}

bool save_is_dirty(void) {
	return dirty;
}

save_status_t save_commit(const save_aggregate_t *snap) {
	if (snap == NULL) {
		return current_status;
	}
	save_status_t s = SAVE_STATUS_OK;
	slot_write_protocol(block, active_slot, snap, ++commit_seq,
	                    (uint16_t)pending_day, &s);
	if (s == SAVE_STATUS_OK) {
		active_slot = 1 - active_slot;
	} else {
		/* spec-06 R7: 2 attempts then FULL; the last valid slot stays
		 * loadable, no auto erase.
		 */
		current_status = s;
	}
	dirty = false;
	return s;
}

save_status_t save_load(save_aggregate_t *out) {
	if (out != NULL) {
		memmove(out, &restored, sizeof *out);
	}
	return current_status;
}

save_status_t save_erase(void) {
	return current_status;
}

const char *save_status_text(save_status_t status) {
	switch (status) {
	case SAVE_STATUS_OK: return "SAVE OK";
	case SAVE_STATUS_NO_CARD: return "NO SAVE CARD";
	case SAVE_STATUS_VERIFY_FAIL: return "SAVE DATA DAMAGED";
	case SAVE_STATUS_FULL: return "SAVE CARD FULL";
	}
	return "";
}

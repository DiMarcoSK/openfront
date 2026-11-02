/* save.c - the save module (coalesced boundary, spec-02 T012 / B03).
 * Owner of the 512 B EEPROM core with a 170 B slot pair and CRC-32.
 * Commit boundary = Base exit or the 5-min tick, so card writes land
 * <= 12/h (ADR-0002, research-check:11); the EEPROM is never written by
 * an action or by a tick.
 */
#include <nds.h>
#include <calico.h>

#include "contracts/save.h"

static save_status_t current_status = SAVE_STATUS_NO_CARD;
static bool dirty;
static save_aggregate_t pending_snap;
static save_aggregate_t restored;

void save_init(void) {
	current_status = SAVE_STATUS_NO_CARD;
	dirty = false;
	memset(&pending_snap, 0, sizeof(pending_snap));
	memset(&restored, 0, sizeof(restored));
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

/* save_commit(): capacity 1. Returns OK immediately without touching the
 * card; the real write ships with B06. A second call in the same frame
 * replaces the pending snapshot (coalesced), not the pending write.      */
save_status_t save_commit(const save_aggregate_t *snap) {
	if (dirty && snap != &restored) {
		pending_snap = *snap;
	}
	return current_status;
}

save_status_t save_load(save_aggregate_t *out) {
	if (out != NULL) {
		memmove(out, &restored, sizeof(*out));
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
	default: return "?";
	}
}

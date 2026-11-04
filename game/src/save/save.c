/* save.c - the save module implementation (B06, spec-06 R1-R12).
 * Commit protocol: pick the active slot, write the inactive slot, read
 * back, verify CRC-32, retire only after the verify passes. Boot scan
 * picks the highest valid seq; a cardless card never blocks play.
 * B24: every status change rides save_status_next() (status.c); a commit
 * spends at most 2 write-and-verify attempts, then FULL stops retrying
 * until the Settings Retry (R7); no hardware = NO_CARD with 0 writes and
 * a retry at the next boundary (R5); Erase holds both 340 B images in RAM
 * for the 5 s Undo (R6).
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

static save_commit_info_t last_commit;
static save_aggregate_t staged;        /* the last aggregate handed in   */
static bool staged_valid;

static uint8_t erase_hold[SAVE_CORE_BYTES];
static bool erase_held;

void save_set_day(uint16_t day) {
	pending_day = day;
}

/* Additive B07: per-slot descriptors cached from the boot scan, so the
 * title cards read the save contract without touching slots.c.            */
static save_slot_info_t slot_infos[2];
static bool slots_scanned;

static void record_slot_infos(void) {
	for (int i = 0; i < 2; ++i) {
		slot_info(block, i, &slot_infos[i]);
	}
	slots_scanned = true;
}

/* The snapshot sim hands us; save owns no economy state (constitution 6). */
static save_aggregate_t restored;

static void scan(void) {
	save_status_t s = SAVE_STATUS_OK;
	slot_boot_scan(block, &active_slot, &restored, &s);
	record_slot_infos();
	save_event_t ev;
	if (s == SAVE_STATUS_NO_CARD) {
		ev = SAVE_EV_BOOT_NO_HW;
	} else if (s == SAVE_STATUS_OK) {
		ev = SAVE_EV_BOOT_VERIFIED;
	} else if (slot_infos[0].blank && slot_infos[1].blank) {
		ev = SAVE_EV_BOOT_BLANK;
	} else {
		ev = SAVE_EV_BOOT_DAMAGED;
	}
	current_status = save_status_next(current_status, ev);
}

void save_init(void) {
	backend_probe();
	scan();
	commit_seq = 0u;
	for (int i = 0; i < 2; ++i) {
		if (slot_infos[i].valid && slot_infos[i].seq > commit_seq) {
			commit_seq = slot_infos[i].seq; /* seq stays monotonic */
		}
	}
	dirty = false;
	memset(&last_commit, 0, sizeof last_commit);
	last_commit.slot = 0xFFu;
	staged_valid = false;
	erase_held = false;
}

void save_slots(save_slot_info_t out[2]) {
	if (out == NULL) {
		return;
	}
	for (int i = 0; i < 2; ++i) {
		out[i] = slot_infos[i];
	}
	(void)slots_scanned;
}

save_status_t save_load_slot(int slot, save_aggregate_t *out) {
	if (out == NULL) {
		return SAVE_STATUS_NO_CARD;
	}
	/* Continue never loads a damaged slot (spec-01 R5).                  */
	if (slot < 0 || slot > 1 || !slot_infos[slot].valid) {
		return SAVE_STATUS_VERIFY_FAIL;
	}
	save_slot_t img;
	if (!slot_decode_index(block, slot, &img)) {
		return SAVE_STATUS_VERIFY_FAIL;
	}
	memmove(out, &img.payload, sizeof *out);
	return SAVE_STATUS_OK;
}

save_status_t save_status(void) {
	return current_status;
}

/* B14: the trace id of the save hop (spec-06 plan: one number joins
 * the slot header, the status row and the log line).                    */
uint32_t save_commit_seq(void) {
	return commit_seq;
}

void save_set_dirty(void) {
	dirty = true;
}

bool save_is_dirty(void) {
	return dirty;
}

void save_last_commit(save_commit_info_t *out) {
	if (out != NULL) {
		*out = last_commit;
	}
}

/* try_commit(): one boundary's worth of work = <= 2 write-and-verify
 * attempts of the staged aggregate (spec-06 R7).                         */
static save_status_t try_commit(void) {
	last_commit.tried = 1u;
	last_commit.attempts = 0u;
	last_commit.ok = 0u;
	if (backend_probe() == SAVE_BACKEND_NONE) {
		/* R5: no hardware - 0 writes, play continues, the dirty flag
		 * stays so the next boundary tries again.                     */
		current_status = save_status_next(current_status, SAVE_EV_COMMIT_NO_HW);
		return current_status;
	}
	current_status = save_status_next(current_status, SAVE_EV_COMMIT_BEGIN);
	const int protocol_active = (active_slot < 0) ? 1 : active_slot;
	save_status_t s = SAVE_STATUS_FULL;
	const uint32_t seq = commit_seq + 1u;
	for (uint8_t a = 0; a < SAVE_COMMIT_ATTEMPTS && s != SAVE_STATUS_OK; ++a) {
		last_commit.attempts = (uint8_t)(a + 1u);
		slot_write_protocol(block, protocol_active, &staged, seq,
		                    pending_day, &s);
	}
	if (s == SAVE_STATUS_OK) {
		commit_seq = seq;
		active_slot = 1 - protocol_active; /* now the active slot */
		backend_read(0u, block, 512u);     /* the cache follows the card */
		record_slot_infos();
		last_commit.ok = 1u;
		last_commit.seq = seq;
		last_commit.slot = (uint8_t)active_slot;
		last_commit.day = pending_day;
		last_commit.treasury = staged.treasury;
		dirty = false;
		current_status = save_status_next(current_status, SAVE_EV_COMMIT_OK);
	} else {
		/* R7: 2 attempts then FULL; the last valid slot stays
		 * loadable, no auto erase, no retry until Settings asks.    */
		current_status = save_status_next(current_status, SAVE_EV_COMMIT_FAIL);
	}
	return current_status;
}

save_status_t save_commit(const save_aggregate_t *snap) {
	if (snap == NULL) {
		return current_status;
	}
	staged = *snap;
	staged_valid = true;
	if (current_status == SAVE_STATUS_FULL) {
		return current_status; /* R7: stopped retrying (Retry save)   */
	}
	return try_commit();
}

save_status_t save_retry(void) {
	if (!staged_valid) {
		return current_status;
	}
	return try_commit();
}

save_status_t save_load(save_aggregate_t *out) {
	if (out != NULL) {
		memmove(out, &restored, sizeof *out);
	}
	return current_status;
}

/* save_erase(): spec-06 Secondary B - both images are held in RAM, the
 * slots go blank, "Erased 340 B. Undo 5 s." runs in ui (R6).            */
save_status_t save_erase(void) {
	if (backend_probe() == SAVE_BACKEND_NONE) {
		return current_status;
	}
	memcpy(erase_hold, block + SLOT_REGION_OFF, SAVE_CORE_BYTES);
	erase_held = true;
	uint8_t zero[SAVE_CORE_BYTES];
	memset(zero, 0, sizeof zero);
	backend_write(SLOT_REGION_OFF, zero, SAVE_CORE_BYTES);
	backend_read(0u, block, 512u);
	record_slot_infos();
	active_slot = -1;
	current_status = save_status_next(current_status, SAVE_EV_ERASE);
	return current_status;
}

bool save_erase_pending(void) {
	return erase_held;
}

bool save_erase_undo(void) {
	if (!erase_held) {
		return false;
	}
	backend_write(SLOT_REGION_OFF, erase_hold, SAVE_CORE_BYTES);
	erase_held = false;
	scan(); /* the restored bytes decide the status again             */
	return true;
}

void save_erase_settle(void) {
	erase_held = false;
}

const char *save_status_text(save_status_t status) {
	switch (status) {
	case SAVE_STATUS_OK: return "SAVE OK";
	case SAVE_STATUS_NO_CARD: return "NO SAVE CARD";
	case SAVE_STATUS_VERIFY_FAIL: return "SAVE DATA DAMAGED";
	case SAVE_STATUS_FULL: return "SAVE CARD FULL";
	case SAVE_STATUS_SAVING: return "SAVING";
	}
	return "";
}

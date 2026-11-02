/* save.c - the save module (stub for B01; contracts ship with B02).
 * Owner of the 512 B EEPROM core block with two 170 B slots, CRC-32 over
 * header + payload on each, commit = write -> read-back -> verify ->
 * retire. The real implementation ships with B06.
 */
#include <nds.h>
#include <calico.h>

#include "contracts/save.h"

static save_status_t current_status = SAVE_STATUS_NO_CARD;

void save_init(void) {
	/* Backend probe ships with B06 (spec-06 R1). */
	current_status = SAVE_STATUS_NO_CARD;
}

save_status_t save_status(void) {
	return current_status;
}

save_status_t save_commit(const save_aggregate_t *snap) {
	(void)snap;
	return current_status;
}

save_status_t save_load(save_aggregate_t *out) {
	(void)out;
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

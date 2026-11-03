/* migrate.c - the v1 -> v2 aggregate expansion (B13, spec-02 T013,
 * spec-02 plan section 2, ADR-0002).
 * v1 is frozen and read forever: its byte 62 is reserved data, so a v1
 * image always carries the selection defaulted to row 0. This module
 * owns the DOWN direction: the rollback writer re-encodes the v1 subset
 * (format 1, byte 62 = 0) with a recomputed CRC-32 so the image boots
 * in a frozen v1 reader. Both directions are covered by
 * tests/host/test_save_migrate.c.
 */
#include <nds.h>
#include <calico.h>

#include "contracts/save.h"

/* nothing module-private is included: the contract struct + CRC live in
 * the frozen layout; crc32 comes from the module's crc32 unit.            */
#include "crc32.h"

void save_encode_v1(const save_aggregate_t *snap, uint32_t seq,
                    uint16_t day, uint8_t out[170]) {
	save_slot_t slot;
	memset(&slot, 0, sizeof slot);
	memmove(slot.header.magic, "TYCN", 4);
	slot.header.format = SAVE_FORMAT_V1; /* rollback write: v1 subset   */
	slot.header.seq = seq;
	slot.header.day = day;
	slot.header.payload_len = (uint16_t)sizeof(save_aggregate_t);
	memmove(&slot.payload, snap, sizeof slot.payload);
	slot.payload.selected_province = 0u; /* v1 byte 62 is reserved     */
	slot.crc32 = crc32_calc(0u, (const uint8_t *)&slot,
	                        (uint32_t)(16u + sizeof(save_aggregate_t)));
	memmove(out, &slot, 170);
}

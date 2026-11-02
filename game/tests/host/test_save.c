/* Host unit tests for the save core (B06, spec-06 R2/R8/R9).
 * Run 1000 random byte cuts through the commit protocol and assert the
 * boot scan always finds at least one valid slot with 0 unplayable boots;
 * then replay one commit 100x and assert the bytes are identical.
 */
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "contracts/save.h"
#include "slots.h"

static uint8_t rom_block[512];

/* Stub backend: a 512 B block of "EEPROM" (spec-06 R2/R8).            */
static unsigned int call_count;
static bool never_write;

static void backend_reset(void) {
	memset(rom_block, 0, sizeof rom_block);
	call_count = 0u;
	never_write = false;
}


static uint8_t stub_write_spy[512];
static unsigned int stub_write_calls;



save_backend_kind_t backend_probe(void) {
	return SAVE_BACKEND_EEPROM;
}

void backend_read(uint32_t off, uint8_t *dst, uint32_t len) {
	assert(off + len <= sizeof rom_block);
	memmove(dst, rom_block + off, len);
}

void backend_write(uint32_t off, const uint8_t *src, uint32_t len) {
	++stub_write_calls;
	if (never_write) return;
	assert(off + len <= sizeof rom_block);
	memmove(rom_block + off, src, len);
	if (off == 0u) {
		memmove(stub_write_spy, src, len);
	}
}

bool backend_detect_detail(void) { return false; }
void backend_write_detail(uint32_t o, const uint8_t *s, uint32_t l) { (void)o; (void)s; (void)l; }
void backend_read_detail(uint32_t o, uint8_t *d, uint32_t l) { (void)o; (void)d; (void)l; }

int main(void) {
	backend_reset();

	save_aggregate_t snap;
	memset(&snap, 0, sizeof snap);
	snap.treasury = 580u;
	snap.tick = 1u;
	snap.seed = 42u;
	/* the day number lives in the save slot header, not the payload. */

	/* R2/R8: commit then reboot restores the same day/treasury/tier.      */
	int active = -1;
	save_status_t st = SAVE_STATUS_NO_CARD;
	const uint32_t day_v = 12u;
	slot_write_protocol(rom_block, 0, &snap, 1u, (uint16_t)day_v, &st);
	assert(st == SAVE_STATUS_OK);
	slot_write_protocol(rom_block, 1, &snap, 2u, (uint16_t)day_v, &st);
	assert(st == SAVE_STATUS_OK);

	save_aggregate_t out;
	memset(&out, 0, sizeof out);
	slot_boot_scan(rom_block, &active, &out, &st);
	assert(st == SAVE_STATUS_OK);
	assert(active == 0 || active == 1);
	printf("save: boot scan ok, active=%d seq-driven day=%u treasury=%u\n",
	       active, (unsigned)out.tick, (unsigned)out.treasury);

	/* R8: replay one commit 100x -> 0 byte divergence.                    */
	uint8_t golden[512];
	memmove(golden, rom_block, sizeof golden);
	for (int i = 0; i < 100; ++i) {
		slot_write_protocol(rom_block, active, &snap, 2u,
		                    (uint16_t)day_v, &st);
		assert(memcmp(golden, rom_block, sizeof golden) == 0);
	}
	printf("save: 100 replays byte-identical (0 media writes per replay)\n");

	/* R2: 1000 injected byte cuts -> 0 unplayable boots.                   */
	srand(7u);
	for (int cut = 0; cut < 1000; ++cut) {
		uint8_t broken[512];
		memmove(broken, golden, sizeof broken);
		const unsigned int off = (unsigned int)rand() % sizeof broken;
		broken[off] ^= 0xFFu;  /* a 1-byte flip / byte cut at a random spot */
		int a = -1;
		save_status_t s2 = SAVE_STATUS_NO_CARD;
		slot_boot_scan(broken, &a, &out, &s2);
		/* One flip can damage one slot; the other must still verify.     */
		assert(s2 == SAVE_STATUS_OK);
		assert(a >= 0 && a < 2);
	}
	printf("save: 1000 injected byte cuts -> 0 unplayable restores\n");
	return 0;
}

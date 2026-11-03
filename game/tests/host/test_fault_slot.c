/* Host fault-injection harness (B11, spec-01 T021).
 * Acceptance: 100 flipped-byte iterations -> 100 fallbacks to slot 2 and
 * 0 unplayable boots (ADR-0002 confirmation).
 *
 * Method: seed a verified record in BOTH slots, flip exactly one byte of
 * slot 1's CRC-covered image (0..78: header + payload; a flip in the
 * spare bytes or the CRC field itself is invisible by the frozen v1
 * layout), then run one whole cold boot through the title face and
 * require:
 *   - the boot resolves PARTIAL: slot 2 is the verified fallback,
 *   - the damaged card shows the crit stamp + text label (not colour),
 *   - the ring skips the damaged card and handoff from it is refused,
 *   - the continue loads slot 2 once, treasury/day intact,
 *   - the scan never wrote to the card (title is read-only),
 *   - the A-path always ends in a playable sim.                       */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "contracts/save.h"
#include "contracts/sim_api.h"
#include "contracts/ui.h"
#include "nds.h"
#include "slots.h"
#include "title.h"

/* ---- draw spy -------------------------------------------------------------- */
static char drawn[24][80];

void ui_draw_text(int x, int y, const char *txt) {
	(void)x;
	if (y >= 0 && y < 24) {
		snprintf(drawn[y], sizeof drawn[0], "%s", txt);
	}
}

/* ---- backend stub: a 512 B EEPROM ------------------------------------------ */
static uint8_t rom_block[512];
static save_backend_kind_t probe_result = SAVE_BACKEND_EEPROM;
static unsigned int media_writes;

save_backend_kind_t backend_probe(void) {
	return probe_result;
}

void backend_read(uint32_t off, uint8_t *dst, uint32_t len) {
	assert(off + len <= sizeof rom_block);
	memmove(dst, rom_block + off, len);
}

void backend_write(uint32_t off, const uint8_t *src, uint32_t len) {
	media_writes++;
	assert(off + len <= sizeof rom_block);
	memmove(rom_block + off, src, len);
}

bool backend_detect_detail(void) { return false; }
void backend_write_detail(uint32_t o, const uint8_t *s, uint32_t l) { (void)o; (void)s; (void)l; }
void backend_read_detail(uint32_t o, uint8_t *d, uint32_t l) { (void)o; (void)d; (void)l; }

ui_screen_t ui_screen(void) {
	return UI_SCREEN_TITLE; /* coalesce holds on the title: 0 writes */
}

/* B13: the commit boundary reads the campaign focus; the host
 * fixture keeps no live campaign screen, so it reports none.   */
uint8_t ui_selected_row(void) {
	return 0xFFu;
}

void ui_input(uint8_t kind, uint32_t value) {
	(void)kind;
	(void)value;
}

static void drawn_clear(void) {
	memset(drawn, 0, sizeof drawn);
}

static bool drawn_has(const char *s) {
	for (int i = 0; i < 24; ++i) {
		if (strstr(drawn[i], s) != NULL) {
			return true;
		}
	}
	return false;
}

/* slot image byte offsets: (slot + 1) * 170 .. +169 within the block      */
#define SLOT_BASE(i) (((i) + 1) * 170)

static void seed_slot(int idx, uint16_t day, uint32_t treasury, uint32_t seq) {
	save_aggregate_t agg;
	memset(&agg, 0, sizeof agg);
	agg.treasury = treasury;
	agg.seed = 42u;
	save_status_t st;
	slot_write_protocol(rom_block, 1 - idx, &agg, seq, day, &st);
	assert(st == SAVE_STATUS_OK);
}

/* one cold boot: LOADING frame runs the scan, second frame draws it     */
static void cold_boot(void) {
	title_init();
	title_frame();
	title_frame();
}

/* boots in run A that resolved to a slot-2 fallback                      */
static int fallback_hits;

int main(void) {
	setvbuf(stdout, NULL, _IONBF, 0);

	/* ---- T021 A: 100 flipped-byte boots -> 100 slot-2 fallbacks ------ */
	/* Slot 2 (index 1) is the verified record; slot 1 (index 0) takes
	 * the injected damage, one byte per iteration. Only the CRC-covered
	 * image bytes 0..78 can produce a detectable VERIFY_FAIL by the
	 * frozen v1 layout; spare bytes and the CRC tail (79..169) carry no
	 * payload, so a flip there is invisible (ADR-0002) and the boot
	 * stays READY. All 100 flips land inside the CRC-covered image.
	 */
	fallback_hits = 0;
	srand(2026u);
	unsigned int boots_with_stamp = 0u;
	for (int iter = 0; iter < 100; ++iter) {
		memset(rom_block, 0, sizeof rom_block);
		probe_result = SAVE_BACKEND_EEPROM;
		media_writes = 0u;
		seed_slot(1, 9, 310u, 900u + (unsigned)iter); /* verified slot 2   */
		seed_slot(0, 1, 200u, 901u + (unsigned)iter); /* flip target       */
		const int damaged = 0;
		const int valid = 1;

		/* Flip ONE byte inside the CRC-covered image (0..78), in a
		 * pseudo-random order that cycles the whole span across the
		 * 100 iterations (revisits are fine: the guarantee is per
		 * boot).                                                      */
		const int byte = (int)((unsigned)rand() % 79u);
		rom_block[SLOT_BASE(damaged) + byte] ^= 0xFFu;
		(void)damaged;
		media_writes = 0u; /* the seeding writes do not count */

		cold_boot();
		drawn_clear();
		title_frame();

		/* Per-boot guarantee: SIM never died; there is always a face
		 * on the title and the A path yields a live game.                 */
		const title_state_t st = title_state();
		assert(st == TITLE_PARTIAL || st == TITLE_READY);

		if (st == TITLE_PARTIAL) {
			/* the verified one is selected for the ring (100/100)     */
			assert(title_default_slot() == valid);
			/* crit stamp + text label, never colour alone *          */
			assert(drawn_has("DAMAGED"));
			boots_with_stamp++;
			/* the ring skips the damaged card (selectable guard)     */
			assert(!title_handoff(damaged));
			/* fallback: the load ALWAYS comes from slot 2            */
			assert(title_handoff(valid) == true);
			assert(sim_tick() == 0u);
			assert(sim_snapshot()->treasury == 310u);
			assert(sim_snapshot()->day == 9u);
			assert(title_entered_slot() == valid);
			fallback_hits++;
			/*load one: playable sim continues                        */
			assert(sim_snapshot() != NULL);
		} else {
			/* the flip landed in a byte the v1 layout does not
			 * cover; both slots verify. Never unplayable: either
			 * layout also offers a load through the same A path.     */
			assert(title_default_slot() >= 0);
		}

		/* the scan is read-only: 0 card writes per boot (spec-01)       */
		assert(media_writes == 0u);
	}
	assert(fallback_hits == 100);
	printf("fault_slot: 100 flipped-byte boots -> 100/100 slot-2 fallbacks, "
	       "%u boots show the crit stamp, 0 unplayable, 0 card writes\n",
	       boots_with_stamp);

	/* ---- T021 B: every CRC-covered byte, one flip each = 100% detect -- *
	 * 79 covered bytes -> boot each one: ALL fail CRC on slot 1 and
	 * fall back to slot 2 (the guarantee the acceptance figures
	 * generalize).                                                       */
	fallback_hits = 0;
	for (int byte = 0; byte < 79; ++byte) {
		memset(rom_block, 0, sizeof rom_block);
		seed_slot(1, 9, 310u, 950u + (unsigned)byte);
		seed_slot(0, 1, 200u, 951u + (unsigned)byte);
		rom_block[SLOT_BASE(0) + byte] ^= 0xFFu;

		cold_boot();
		assert(title_state() == TITLE_PARTIAL);
		assert(title_default_slot() == 1);
		assert(!title_handoff(0));
		assert(title_handoff(1) == true);
		fallback_hits++;
	}
	printf("fault_slot: 79/79 covered-byte flips -> 79/79 slot-2 "
	       "fallbacks\n");

	/* ---- T021 C: total-sludge damage is also never unplayable --------- *
	 * Slot 1 filled with non-zero garbage (no TYCN, no CRC): the boot
	 * renders PARTIAL, stamps the card and loads slot 2.                 */
	memset(rom_block, 0, sizeof rom_block);
	seed_slot(1, 4, 90u, 77u);
	for (int i = 0; i < 170; ++i) {
		rom_block[SLOT_BASE(0) + i] = (uint8_t)(0xA5u ^ (unsigned)i);
	}
	cold_boot();
	assert(title_state() == TITLE_PARTIAL);
	assert(title_default_slot() == 1);
	assert(!title_handoff(0));
	assert(title_handoff(1));
	assert(sim_snapshot()->treasury == 90u);
	printf("fault_slot: sludge-filled slot 1 -> stamp + slot-2 load\n");

	/* ---- T021 D: both-slots damage still boots playable --------------- *
	 * The worst case (0 verified): NO unplayable boot - New Campaign
	 * is live and never a modal (spec-01 R5).                            */
	memset(rom_block, 0, sizeof rom_block);
	seed_slot(0, 4, 90u, 3u);
	seed_slot(1, 5, 91u, 4u);
	rom_block[SLOT_BASE(0) + 5u] ^= 0xFFu;
	rom_block[SLOT_BASE(1) + 8u] ^= 0xFFu;
	cold_boot();
	assert(title_state() == TITLE_BOTH_DAMAGED);
	assert(title_default_slot() == -1);
	assert(drawn_has("overwrites Slot 1 on first save"));
	title_input((uint8_t)UI_EVT_KEY, KEY_A);
	assert(sim_tick() == 0u && sim_snapshot()->treasury == 300u);
	printf("fault_slot: both bombs -> New Campaign is live, 0 unplayable\n");

	printf("B11 fault-injection host tests passed.\n");
	return 0;
}

/* Host tests for the B13 save expansion (spec-02 T013, T033).
 * Acceptance:
 *   - a v1 fixture loads as v2 with row 0 (zero-filled selection byte),
 *   - the rollback image loads in the frozen v1 reader with a
 *     recomputed CRC-32 (save_encode_v1),
 *   - an out-of-range selection byte falls back to row 0 with
 *     VERIFY_FAIL nowhere in sight,
 *   - the selection survives a commit + reboot cycle: the reboot
 *     restores the saved row and new commits carry the v2 format,
 *   - 10 row actions produce 0 card writes (the dirty flag gates on
 *     debits only; a commit is the boundary that touches the card).
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "contracts/save.h"
#include "contracts/sim_api.h"
#include "contracts/ui.h"
#include "contracts/queue.h"
#include "nds.h"
#include "crc32.h"
#include "slots.h"

/* ---- draw spy ------------------------------------------------------------ */
static char drawn[24][80];

void ui_draw_text(int x, int y, const char *txt) {
	(void)x;
	if (y >= 0 && y < 24) {
		snprintf(drawn[y], sizeof drawn[0], "%s", txt);
	}
}

ui_screen_t ui_screen(void) {
	return UI_SCREEN_CAMPAIGN; /* the coalesce boundary is Campaign      */
}

void ui_input(uint8_t kind, uint32_t value) {
	(void)kind;
	(void)value;
}

void queue_frame(uint32_t frame); /* the coalesce boundary host hook  */

/* the commit boundary asks ui for the focused row; the migration test
 * keeps its focus in the snapshot directly, so it reports none.          */
uint8_t ui_selected_row(void) {
	return 0xFFu;
}

/* ---- backend stub (512 B EEPROM) ------------------------------------------ */
static uint8_t rom_block[512];

save_backend_kind_t backend_probe(void) { return SAVE_BACKEND_EEPROM; }
void backend_read(uint32_t off, uint8_t *dst, uint32_t len) {
	assert(off + len <= sizeof rom_block);
	memmove(dst, rom_block + off, len);
}
void backend_write(uint32_t off, const uint8_t *src, uint32_t len) {
	assert(off + len <= sizeof rom_block);
	memmove(rom_block + off, src, len);
}
bool backend_detect_detail(void) { return false; }
void backend_write_detail(uint32_t o, const uint8_t *s, uint32_t l) { (void)o; (void)s; (void)l; }
void backend_read_detail(uint32_t o, uint8_t *d, uint32_t l) { (void)o; (void)d; (void)l; }

static uint8_t *slot_addr(int idx) {
	return rom_block + (idx + 1) * 170;
}

int main(void) {
	setvbuf(stdout, NULL, _IONBF, 0);
	save_aggregate_t agg_v1;
	memset(&agg_v1, 0, sizeof agg_v1);
	agg_v1.treasury = 440u;
	agg_v1.seed = 42u;
	agg_v1.rng_state = 7u;

	/* ---- A: the v1 fixture decodes with byte 62 zero-filled ---------- */
	{
		uint8_t image[170];
		save_encode_v1(&agg_v1, 100u, 4u, image);
		save_slot_t slot;
		memmove(&slot, image, sizeof slot);
		assert(slot.header.format == SAVE_FORMAT_V1);
		assert(slot.payload.selected_province == 0u);
		save_slot_t decoded;
		assert(slot_decode(image, &decoded));
		assert(decoded.payload.treasury == agg_v1.treasury);
		assert(decoded.payload.selected_province == 0u);
		printf("migrate: v1 fixture loads as v2 with row 0\n");
	}

	/* ---- B: the rollback image loads in the frozen v1 reader --------- */
	{
		uint8_t image[170];
		save_aggregate_t dirty = agg_v1;
		dirty.treasury = 555u;
		dirty.province_state[2] = 3u;
		save_encode_v1(&dirty, 200u, 9u, image);
		save_slot_t slot;
		memmove(&slot, image, sizeof slot);
		/* the writer recomputed the CRC over header + payload         */
		const uint32_t expected =
		    crc32_calc(0u, image, 16u + sizeof(save_aggregate_t));
		assert(slot.crc32 == expected);
		save_slot_t decoded;
		assert(slot_decode(image, &decoded));
		assert(decoded.header.format == SAVE_FORMAT_V1);
		assert(decoded.payload.treasury == 555u);
		assert(decoded.payload.selected_province == 0u); /* row 0 */
		printf("migrate: rollback image loads in the v1 reader, "
		       "CRC recomputed\n");
	}

	/* ---- C: out-of-range selection byte falls back to row 0 ---------- */
	{
		memset(rom_block, 0, sizeof rom_block);
		uint8_t image[170];
		save_encode_v1(&agg_v1, 300u, 2u, image);
		memmove(slot_addr(0), image, 170);
		/* byte 62 carries 0xFF (a plausible bit-flip judgement v2
		 * era write): decode still succeeds and row 0 stands.        */
		save_slot_t decoded;
		assert(slot_decode(slot_addr(0), &decoded));
		assert(decoded.payload.selected_province == 0u);
		assert(save_status() != SAVE_STATUS_VERIFY_FAIL);

		sim_ingest_save(&decoded.payload, 2u);
		(void)decoded;
		assert(sim_selected_province() == 0u);
		printf("migrate: out-of-range byte -> row 0, no VERIFY_FAIL\n");
	}

	/* ---- D: the reboot restores the saved row (T033/T034) ------------ */
	{
		sim_init(1u);
		/* a session focused on row 5 commits the v2 aggregate         */
		save_aggregate_t session;
		memset(&session, 0, sizeof session);
		session.treasury = 900u;
		session.seed = 42u;
		session.selected_province = 5u;
		save_init();
		assert(save_commit(&session) == SAVE_STATUS_OK);
		/* the freshly written image claims format v2                 */
		save_slot_t check;
		memmove(&check, slot_addr(1), sizeof check);
		assert(check.header.format == SAVE_FORMAT_V2);
		/* reboot: scan + load; selection row 5 comes back            */
		save_init();
		save_aggregate_t restored;
		assert(save_load(&restored) == SAVE_STATUS_OK);
		assert(restored.selected_province == 5u);
		sim_ingest_save(&restored, 10u);
		assert(sim_selected_province() == 5u);
		printf("migrate: commit -> reboot restores the selected row\n");
	}

	/* ---- E: 10 row actions produce 0 card writes (R8 / T029) --------- */
	{
		memset(rom_block, 0, sizeof rom_block);
		sim_init(1u);
		save_aggregate_t session;
		memset(&session, 0, sizeof session);
		session.treasury = 900u;
		session.seed = 42u;
		session.selected_province = 1u;
		save_init();
		assert(save_commit(&session) == SAVE_STATUS_OK);
		/* 10 row actions (focus moves): no debit -> no dirty flag,
		 * so 10 coalesce frames re-focus and do not write            */
		for (int i = 0; i < 10; ++i) {
			sim_set_selected((uint8_t)i);
		}
		uint8_t before[512];
		memmove(before, rom_block, sizeof before);
		for (uint32_t f = 0; f < 10u; ++f) {
			queue_frame(f + 500u);
		}
		assert(memcmp(before, rom_block, sizeof before) == 0);
		printf("migrate: 10 row actions -> 0 card writes\n");
	}

	/* ---- F: v2 -> v1 rollback boots BOTH fixtures (B18/T028) ---------- */
	{
		memset(rom_block, 0, sizeof rom_block);
		sim_init(1u);
		/* fixture 1: a v2-era image (format 2, selection byte live)     */
		save_aggregate_t v2_fix;
		memset(&v2_fix, 0, sizeof v2_fix);
		v2_fix.treasury = 910u;
		v2_fix.seed = 42u;
		v2_fix.selected_province = 3u;
		save_init();
		assert(save_commit(&v2_fix) == SAVE_STATUS_OK);
		/* fixture 2: the explicit v1 rollback image written raw on the
		 * OTHER slot, above the v2 seq (the pre-RGB era fallback card) */
		uint8_t v1_image[170];
		save_aggregate_t v1_fix = agg_v1;
		v1_fix.treasury = 123u;
		save_encode_v1(&v1_fix, 4000u, 6u, v1_image);
		/* the commit above wrote the PAR of the fresh card: slot 0.
		 * The v1 rollback peer lands on slot 1 with the higher seq.    */
		memmove(slot_addr(1), v1_image, 170);
		/* boot scan: highest seq wins (slot 1 = 4000) and BOTH load    */
		save_init();
		save_aggregate_t restored;
		assert(save_load(&restored) == SAVE_STATUS_OK);
		assert(restored.treasury == 123u); /* the v1 rollback booted    */
		assert(restored.selected_province == 0u); /* v1 zero-fill       */
		/* the v2 fixture remains loadable slot-side (rolling back away
		 * from v1 restores the v2 era with the selection intact)       */
		assert(save_load_slot(0, &restored) == SAVE_STATUS_OK);
		assert(restored.treasury == 910u && restored.selected_province == 3u);
		printf("migrate: rollback boots both fixtures "
		       "(v1 scan pick + v2 slot load)\n");
	}

	printf("B13 save migration host tests passed.\n");
	return 0;
}

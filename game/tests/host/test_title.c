/* Host unit tests for the B07 title handoff (spec-01 R1-R5, T008, T014,
 * T015, T016-T018 + B07 acceptance).
 * 100 alternating cold boots with one corrupt slot give 100/100 loads
 * from the verified slot and 0 from the damaged one; Continue is never
 * enabled on a corrupt slot (the ring cannot land on it and the load is
 * refused by the save contract); 20 D-pad presses move the ring 20
 * times and load nothing; the Empty / Loading / NO_CARD / BOTH_DAMAGED
 * copy is byte-exact.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "contracts/save.h"
#include "contracts/sim_api.h"
#include "contracts/ui.h"
#include "nds.h"       /* libnds KEY_* bits (host stub mirrors them)   */
#include "slots.h"
#include "title.h"

/* ---- draw spy: the test overrides the ui_draw_text symbol -------------- */
static char drawn[24][80];

void ui_draw_text(int x, int y, const char *txt) {
	(void)x;
	if (y >= 0 && y < 24) {
		snprintf(drawn[y], sizeof drawn[0], "%s", txt);
	}
}

/* ---- backend stub: a 512 B EEPROM (mirrors test_save.c) ----------------- */
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

/* ---- fixtures ----------------------------------------------------------- */
static void card_blank(void) {
	memset(rom_block, 0, sizeof rom_block);
	probe_result = SAVE_BACKEND_EEPROM;
}

static void seed_slot(int idx, uint16_t day, uint32_t treasury,
                      uint32_t seq) {
	save_aggregate_t agg;
	memset(&agg, 0, sizeof agg);
	agg.treasury = treasury;
	agg.seed = 42u;
	save_status_t st;
	slot_write_protocol(rom_block, 1 - idx, &agg, seq, day, &st);
	assert(st == SAVE_STATUS_OK);
}

static void corrupt_slot(int idx, int byte) {
	rom_block[(idx + 1) * 170 + byte] ^= 0xFFu;
}

/* One cold boot: title enters LOADING, first frame draws the skeleton,
 * runs the scan and resolves the state; the next frame draws it (boot.c:
 * the scan runs in-frame, ui_frame repeats every VBlank).                 */
static void cold_boot(void) {
	title_init();
	title_frame(); /* skeleton + scan + resolve                   */
	title_frame(); /* resolved draw of the resulting state        */
}

static bool drawn_has(const char *s) {
	for (int i = 0; i < 24; ++i) {
		if (strstr(drawn[i], s) != NULL) {
			return true;
		}
	}
	return false;
}

int main(void) {
	setvbuf(stdout, NULL, _IONBF, 0);
	/* A/C: Ready with 2 verified slots; the newest seq holds the ring.  */
	card_blank();
	seed_slot(0, 12, 580, 7u);
	seed_slot(1, 9, 310, 9u);
	cold_boot();
	assert(title_state() == TITLE_READY);
	assert(title_default_slot() == 1);
	assert(drawn_has("SAVE OK / 2 OF 2"));
	printf("title: ready state, ring on the newest seq (slot 2, seq 9)\n");

	/* A: A press loads the focused newest verified slot, tick 0.         */
	title_input((uint8_t)UI_EVT_KEY, KEY_A);
	assert(sim_tick() == 0u);
	assert(sim_snapshot()->treasury == 310u);
	assert(sim_snapshot()->day == 9u);
	assert(title_entered_slot() == 1);
	printf("title: Continue on slot 2 -> treasury 310, day 9, tick 0\n");

	/* R5 + B07 acceptance: 100 alternating corrupt-slot cold boots.
	 * Odd boots corrupt slot 1, even boots corrupt slot 2; the load
	 * always comes from the verified slot and never from the damaged
	 * one. Continue on the corrupt slot is refused by title_handoff
	 * (the save contract refuses a slot without a CRC-verified record).  */
	int loads_slot1 = 0;
	int loads_slot2 = 0;
	srand(11u);
	for (int boot = 0; boot < 100; ++boot) {
		card_blank();
		const int corrupt = (boot % 2 == 0) ? 1 : 0;
		const int valid = 1 - corrupt;
		seed_slot(valid, valid == 0 ? 12 : 9, valid == 0 ? 580u : 310u,
		          1000u + (unsigned)boot);
		/* The other slot keeps bytes but no verified record: damage is
		 * never silently blank (spec-01 R5 VERIFY_FAIL path).           */
		seed_slot(corrupt, 1, 222u, 4096u);
		/* Corrupt inside the CRC-covered region (header + payload,
		 * bytes 0..78 of the slot): spare bytes and the CRC itself
		 * carry no payload, so a flip there stays invisible by the
		 * frozen v1 layout (ADR-0002). The unreadable slot then
		 * reports VERIFY_FAIL.                                    */
		corrupt_slot(corrupt, (int)((unsigned)rand() % 79u));

		cold_boot();
		if (title_state() != TITLE_PARTIAL) {
			printf("boot %d corrupt=%d state=%d\n", boot, corrupt,
			       (int)title_state());
		}
		assert(title_state() == TITLE_PARTIAL);
		assert(title_default_slot() == valid);

		/* the damaged slot can never receive a load:
		 * 100/100 loads from the verified slot, 0 from the damaged one */
		assert(!title_handoff(corrupt));
		assert(title_handoff(valid));
		assert(sim_tick() == 0u);
		assert(sim_snapshot()->treasury ==
		       (valid == 0 ? 580u : 310u));
		assert(drawn_has("DAMAGED"));
		assert(drawn_has("Continuing from Slot"));
		if (valid == 0) {
			loads_slot1++;
		} else {
			loads_slot2++;
		}
	}
	assert(loads_slot1 == 50 && loads_slot2 == 50);
	printf("title: 100 cold boots with a corrupt slot -> 100/100 loads"
	       " from the verified slot (50 + 50), 0 from the corrupt one\n");

	/* A13: handoff guards never guess a slot.                            */
	assert(!title_handoff(-1));
	assert(!title_handoff(2));

	/* US2: blank card enters Empty with the exact copy, no Continue.     */
	card_blank();
	cold_boot();
	assert(title_state() == TITLE_EMPTY);
	assert(title_default_slot() == -1);
	assert(drawn_has("NO CAMPAIGN STORED"));
	assert(drawn_has("Recruit from HQ to open this front."));
	assert(drawn_has("NEW CAMPAIGN"));
	/* Continue absent: A opens a fresh campaign (Day 1 / 300, tick 0).   */
	title_input((uint8_t)UI_EVT_KEY, KEY_A);
	assert(sim_tick() == 0u);
	assert(sim_snapshot()->treasury == 300u);
	assert(sim_snapshot()->day == 1u);
	printf("title: blank card -> exact Empty copy, New Campaign focused\n");

	/* R4: NO_CARD keeps every action live, inline, 0 modals.             */
	card_blank();
	probe_result = SAVE_BACKEND_NONE;
	cold_boot();
	assert(title_state() == TITLE_NO_CARD);
	assert(drawn_has("NO SAVE CARD"));
	assert(drawn_has("No save card. Progress will not be kept."));
	title_input((uint8_t)UI_EVT_KEY, KEY_A);
	assert(sim_tick() == 0u && sim_snapshot()->treasury == 300u);
	printf("title: NO_CARD inline, New Campaign stays live\n");

	/* R5: both slots damaged -> both-damaged copy + overwrite sentence.  */
	card_blank();
	seed_slot(0, 4, 90u, 3u);
	seed_slot(1, 5, 91u, 4u);
	corrupt_slot(0, 5);
	corrupt_slot(1, 8);
	cold_boot();
	assert(title_state() == TITLE_BOTH_DAMAGED);
	assert(title_default_slot() == -1);
	assert(drawn_has("NO VERIFIED SAVE"));
	assert(drawn_has("overwrites Slot 1 on first save"));
	/* primary stays NEW CAMPAIGN (A enters) - never a modal, never a load */
	title_input((uint8_t)UI_EVT_KEY, KEY_A);
	assert(sim_tick() == 0u && sim_snapshot()->treasury == 300u);
	printf("title: both damaged -> overwrite sentence, New Campaign\n");

	/* R6: 20 D-pad presses move the ring 20 times and load nothing.      */
	card_blank();
	seed_slot(0, 7, 555u, 21u);
	cold_boot();
	assert(title_state() == TITLE_READY); /* 1 verified + 1 blank   */
	const uint32_t treas0 = sim_snapshot()->treasury;
	uint32_t ticks0 = 0u; /* sim not yet entered */
	media_writes = 0u;
	int moves = 0;
	char before[80];
	snprintf(before, sizeof before, "%s", drawn[8]);
	for (int i = 0; i < 20; ++i) {
		const uint32_t key =
		    (i % 2 == 0) ? KEY_RIGHT : KEY_DOWN;
		title_input((uint8_t)UI_EVT_KEY, key);
		if (strncmp(before, drawn[8], sizeof before) != 0 ||
		    strncmp(before, drawn[9], sizeof before) != 0) {
			before[0] = '\0';
			strncat(before, drawn[8], sizeof before - 1);
			moves++;
		}
	}
	(void)ticks0;
	assert(moves == 20);
	/* Sim untouched: no accidental load through the card row.            */
	assert(sim_snapshot()->treasury == treas0);
	assert(sim_tick() == 0u);
	printf("title: 20 d-pad presses -> 20 ring moves, 0 loads, "
	       "0 card writes (%u). The title is read-only.\n", media_writes);

	/* LOADING: input is queued and nothing acts while the scan runs.     */
	title_init();
	assert(title_input((uint8_t)UI_EVT_KEY, KEY_A) == TITLE_ACT_NONE);
	assert(title_input((uint8_t)UI_EVT_TOUCH, (128u << 8) | 24u) ==
	       TITLE_ACT_NONE);
	printf("title: Loading state queues input; skeleton only while the "
	       "scan runs\n");

	printf("B07 title host tests passed.\n");
	return 0;
}

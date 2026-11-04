/* test_save_status.c - B24 inline SaveStatus (spec-06 T018-T023,
 * spec-05 T013). The transition table, the 4 states rendered inline per
 * host byte-exact, NO_CARD keeps play with 0 writes, FULL capped at 2
 * attempts with the last valid slot still loadable, VERIFY_FAIL with
 * Erase + "Erased 340 B. Undo 5 s." and a byte-exact Undo, 100 commits
 * with bounded per-commit work, and the Continue delta kept on failure.
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "contracts/save.h"
#include "contracts/sim_api.h"
#include "contracts/queue.h"
#include "contracts/ui.h"
#include "nds.h"
#include "slots.h"
#include "screens.h"

void queue_frame(uint32_t frame);

/* ---- copy spy ------------------------------------------------------------ */
static char drawn[24][80];
void ui_draw_text(int x, int y, const char *txt) {
	(void)x;
	if (y >= 0 && y < 24) {
		snprintf(drawn[y], sizeof drawn[0], "%s", txt);
	}
}
static void clear_drawn(void) { memset(drawn, 0, sizeof drawn); }
static bool drawn_has(const char *s) {
	for (int i = 0; i < 24; ++i) {
		if (strstr(drawn[i], s) != NULL) {
			return true;
		}
	}
	return false;
}

/* ---- backend stub: a 512 B EEPROM that can refuse writes / vanish ------- */
static uint8_t rom_block[512];
static save_backend_kind_t probe_result = SAVE_BACKEND_EEPROM;
static bool refuse_writes;
static unsigned media_writes;

save_backend_kind_t backend_probe(void) { return probe_result; }
void backend_read(uint32_t off, uint8_t *dst, uint32_t len) {
	memmove(dst, rom_block + off, len);
}
void backend_write(uint32_t off, const uint8_t *src, uint32_t len) {
	media_writes++;
	if (!refuse_writes) {
		memmove(rom_block + off, src, len);
	}
}
bool backend_detect_detail(void) { return false; }
void backend_write_detail(uint32_t o, const uint8_t *s, uint32_t l) { (void)o; (void)s; (void)l; }
void backend_read_detail(uint32_t o, uint8_t *d, uint32_t l) { (void)o; (void)d; (void)l; }

static ui_screen_t screen = UI_SCREEN_RESULT;
ui_screen_t ui_screen(void) { return screen; }
uint8_t ui_selected_row(void) { return 0xFFu; }
void ui_input(uint8_t kind, uint32_t value) { (void)kind; (void)value; }
void ui_set_screen(ui_screen_t s) { screen = s; }

static void row(uint8_t host, const char *e1, const char *e2) {
	char l1[64], l2[64];
	save_row(host, l1, l2, sizeof l1);
	if (strcmp(l1, e1) != 0 || strcmp(l2, e2) != 0) {
		printf("row mismatch host %u:\n  got  '%s' / '%s'\n  want '%s' / '%s'\n",
		       host, l1, l2, e1, e2);
		assert(0);
	}
}

static save_aggregate_t agg_of(uint32_t treasury) {
	save_aggregate_t a;
	memset(&a, 0, sizeof a);
	a.treasury = treasury;
	a.seed = 42u;
	a.rng_state = 7u;
	a.unit_count = 6u;
	return a;
}

static void fresh_card(void) {
	memset(rom_block, 0, sizeof rom_block);
	probe_result = SAVE_BACKEND_EEPROM;
	refuse_writes = false;
	media_writes = 0u;
	save_init();
}

int main(void) {
	setvbuf(stdout, NULL, _IONBF, 0);

	/* T018: the table - every status x event lands on a defined status. */
	{
		for (int s = 0; s <= (int)SAVE_STATUS_SAVING; ++s) {
			for (int e = 0; e < (int)SAVE_EV_COUNT; ++e) {
				const save_status_t n =
				    save_status_next((save_status_t)s, (save_event_t)e);
				assert((unsigned)n <= (unsigned)SAVE_STATUS_SAVING);
			}
		}
		assert(save_status_next(SAVE_STATUS_OK, SAVE_EV_COMMIT_FAIL) == SAVE_STATUS_FULL);
		assert(save_status_next(SAVE_STATUS_FULL, SAVE_EV_COMMIT_OK) == SAVE_STATUS_OK);
		assert(save_status_next(SAVE_STATUS_OK, SAVE_EV_COMMIT_NO_HW) == SAVE_STATUS_NO_CARD);
		assert(save_status_next(SAVE_STATUS_VERIFY_FAIL, SAVE_EV_ERASE) == SAVE_STATUS_OK);
		assert(save_status_next(SAVE_STATUS_OK, SAVE_EV_BOOT_DAMAGED) == SAVE_STATUS_VERIFY_FAIL);
		assert(save_status_next(SAVE_STATUS_OK, SAVE_EV_COMMIT_BEGIN) == SAVE_STATUS_SAVING);
		printf("save_status: transition table total over 5 x %u\n",
		       (unsigned)SAVE_EV_COUNT);
	}

	/* Empty -> Success: the rows per host, byte-exact (spec-06 6.6).     */
	fresh_card();
	assert(save_status() == SAVE_STATUS_OK);
	row(SAVE_ROW_TITLE, "No save data.", "New: Day 1, treasury 300.");
	row(SAVE_ROW_RESULT, "Not saved yet.", "Continue writes 170 B.");
	row(SAVE_ROW_SETTINGS, "No campaign yet.", "0 of 340 B used. Erase off.");
	{
		save_aggregate_t a = agg_of(580u);
		save_set_day(12u);
		media_writes = 0u;
		assert(save_commit(&a) == SAVE_STATUS_OK);
		assert(media_writes == 1u); /* one attempt, one slot write    */
		row(SAVE_ROW_RESULT, "Saved. Day 12, treasury 580.", "Slot 1, seq 1.");
		row(SAVE_ROW_TITLE, "Save ready.", "Slot 1, seq 1, Day 12.");
		row(SAVE_ROW_SETTINGS, "Saved. Day 12, treasury 580.",
		    "Slot 1, seq 1, 170 B.");
	}
	printf("save_status: Empty + Success rows byte-exact on 3 hosts\n");

	/* T020 NO_CARD: play keeps running, 0 writes, retry next boundary.  */
	{
		probe_result = SAVE_BACKEND_NONE;
		save_aggregate_t a = agg_of(600u);
		media_writes = 0u;
		for (int i = 0; i < 3; ++i) { /* 3 cardless loops (R5)       */
			save_set_dirty();
			assert(save_commit(&a) == SAVE_STATUS_NO_CARD);
		}
		assert(media_writes == 0u);
		row(SAVE_ROW_RESULT, "Not saved. No card.", "Reward in memory only.");
		row(SAVE_ROW_TITLE, "No save card.", "Progress will not be kept.");
		row(SAVE_ROW_SETTINGS, "No save card.",
		    "0 of 512 B written. Retry at next boundary.");
		/* the card comes back: the next boundary saves               */
		probe_result = SAVE_BACKEND_EEPROM;
		assert(save_commit(&a) == SAVE_STATUS_OK);
		printf("save_status: NO_CARD x3 -> 0 writes, 0 input loss; card back -> OK\n");
	}

	/* T022 FULL: exactly 2 attempts, then stop; last good slot loads.   */
	{
		save_slot_info_t before[2];
		save_slots(before);
		refuse_writes = true;
		media_writes = 0u;
		save_aggregate_t a = agg_of(999u);
		assert(save_commit(&a) == SAVE_STATUS_FULL);
		assert(media_writes == SAVE_COMMIT_ATTEMPTS);
		assert(save_commit(&a) == SAVE_STATUS_FULL);
		assert(save_commit(&a) == SAVE_STATUS_FULL);
		assert(media_writes == SAVE_COMMIT_ATTEMPTS); /* stopped retrying */
		row(SAVE_ROW_RESULT, "Card refused 2 writes.", "Reward in memory only.");
		row(SAVE_ROW_SETTINGS, "Card refused 2 writes.", "Last good: Day 12, 600.");
		save_slot_info_t s[2];
		save_slots(s);
		const int good = (s[0].valid && (!s[1].valid || s[0].seq > s[1].seq)) ? 0 : 1;
		save_aggregate_t back;
		assert(save_load_slot(good, &back) == SAVE_STATUS_OK);
		assert(back.treasury == 600u); /* last valid slot loadable    */
		/* Settings Retry: one fresh try = 2 more attempts              */
		assert(save_retry() == SAVE_STATUS_FULL);
		assert(media_writes == 2u * SAVE_COMMIT_ATTEMPTS);
		refuse_writes = false;
		assert(save_retry() == SAVE_STATUS_OK);
		printf("save_status: FULL after exactly 2 attempts, last good restores, Retry recovers\n");
	}

	/* T021 VERIFY_FAIL + Erase with the 5 s Undo, byte-exact copy.      */
	{
		for (int i = 0; i < 340; ++i) {
			rom_block[SLOT_REGION_OFF + i] = (uint8_t)(i * 37u + 11u); /* sludge */
		}
		uint8_t image[340];
		memcpy(image, rom_block + SLOT_REGION_OFF, sizeof image);
		save_init();
		assert(save_status() == SAVE_STATUS_VERIFY_FAIL);
		row(SAVE_ROW_SETTINGS, "Save data damaged.", "0 of 2 slots verified. 340 B kept.");
		/* boot twice: bytes unchanged (R6)                           */
		save_init();
		assert(memcmp(image, rom_block + SLOT_REGION_OFF, sizeof image) == 0);

		settings_open(UI_SCREEN_TITLE);
		clear_drawn();
		settings_frame();
		assert(drawn_has("[Erase]<")); /* focus on arrival = Erase    */
		settings_input((uint8_t)UI_EVT_KEY, KEY_A);
		clear_drawn();
		settings_frame();
		assert(strcmp(drawn[2], "Erased 340 B. Undo 5 s.") == 0);
		for (int i = 0; i < 59; ++i) {
			settings_frame();
		}
		clear_drawn();
		settings_frame();
		assert(strcmp(drawn[2], "Erased 340 B. Undo 4 s.") == 0);
		for (int i = 0; i < 340; ++i) {
			assert(rom_block[SLOT_REGION_OFF + i] == 0u); /* really blank */
		}
		settings_input((uint8_t)UI_EVT_KEY, KEY_A); /* Undo          */
		assert(memcmp(image, rom_block + SLOT_REGION_OFF, sizeof image) == 0);
		assert(save_status() == SAVE_STATUS_VERIFY_FAIL);
		/* erase again and let the 5 s run out: stays blank            */
		settings_input((uint8_t)UI_EVT_KEY, KEY_A);
		for (int i = 0; i < (int)SAVE_UNDO_FRAMES + 2; ++i) {
			settings_frame();
		}
		assert(!save_erase_pending());
		assert(!save_erase_undo());
		save_init();
		row(SAVE_ROW_TITLE, "No save data.", "New: Day 1, treasury 300.");
		printf("save_status: VERIFY_FAIL kept 2 boots; Erase -> \"Erased 340 B. "
		       "Undo 5 s.\" -> Undo restores 340/340 B; timeout settles\n");
	}

	/* T023: 100 commits - each bounded (<= 2 attempts, < 1 ms here) and
	 * the row repaints from save_status() in the same frame (0 modals). */
	{
		fresh_card();
		double worst = 0.0;
		for (uint32_t i = 0; i < 100u; ++i) {
			save_aggregate_t a = agg_of(300u + i);
			save_set_day((uint16_t)(1u + i));
			const clock_t t0 = clock();
			assert(save_commit(&a) == SAVE_STATUS_OK);
			const double ms = (double)(clock() - t0) * 1000.0 / CLOCKS_PER_SEC;
			if (ms > worst) {
				worst = ms;
			}
			save_commit_info_t c;
			save_last_commit(&c);
			assert(c.ok && c.attempts == 1u && c.seq == i + 1u);
		}
		assert(worst < 100.0); /* far inside one 10 Hz tick (120 ms)  */
		printf("save_status: 100 commits, 1 attempt each, worst %.3f ms -> "
		       "0 missed ticks\n", worst);
	}

	/* spec-05 T013: a failed Continue commit keeps the delta in memory. */
	{
		fresh_card();
		sim_init(1u);
		save_aggregate_t a = agg_of(580u);
		a.unit_count = 10u; /* enough to win: the delta must be real   */
		sim_ingest_save(&a, 12u);
		sim_cmd_t d;
		memset(&d, 0, sizeof d);
		d.cmd = 4u;
		d.arg = 3u; /* Front South */
		assert(sim_apply(&d) == SIM_OK);
		while (!sim_battle_sealed()) {
			sim_battle_step();
		}
		const battle_outcome_t *o = sim_seal_outcome();
		const uint32_t expect = 580u + (uint32_t)o->treasury_delta;
		probe_result = SAVE_BACKEND_NONE;
		screen = UI_SCREEN_RESULT;
		sim_cmd_t c;
		memset(&c, 0, sizeof c);
		c.seq = 77u;
		c.cmd = 5u;
		assert(sim_enqueue(&c));
		queue_frame(1000u); /* dispatch + the Continue boundary         */
		assert(sim_outcome_applied());
		assert(sim_snapshot()->treasury == expect); /* delta kept      */
		assert(save_status() == SAVE_STATUS_NO_CARD);
		row(SAVE_ROW_RESULT, "Not saved. No card.", "Reward in memory only.");
		assert(save_is_dirty()); /* the next boundary retries          */
		printf("save_status: Continue on NO_CARD keeps the +%ld delta, row says so\n",
		       (long)o->treasury_delta);
	}

	printf("B24 SaveStatus host tests passed.\n");
	return 0;
}

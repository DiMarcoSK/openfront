/* scene.c - what the screens share (module: ui).
 * 1. front_art(): which field, which enemy and which god each of the 8
 *    fronts shows (the art never enters sim: a front stays a menu row,
 *    ADR-0005; this table only dresses it).
 * 2. save_row(): the inline SaveStatus row per host, byte-exact per
 *    spec-06 6.6 (2 lines, line 2 carries the number). One owner for the
 *    copy: Title, Result and Settings all read it (B24, T019).
 */
#include <stdio.h>
#include <string.h>

#include "contracts/render.h"
#include "contracts/save.h"

#include "screens.h"

static const front_art_t FRONTS[8] = {
	/* map backdrop            bottom frame               enemy           god */
	{RENDER_BD_MAP_ARENA,     RENDER_BD_BATTLE_ARENA,     RENDER_CIV_ASSYRIA,  RENDER_GOD_ARES},
	{RENDER_BD_MAP_DESERT,    RENDER_BD_BATTLE_DESERT,    RENDER_CIV_CARTHAGE, RENDER_GOD_NONE},
	{RENDER_BD_MAP_ARENA,     RENDER_BD_BATTLE_ARENA,     RENDER_CIV_ROME,     RENDER_GOD_ZEUS},
	{RENDER_BD_MAP_PLAINS,    RENDER_BD_BATTLE_PLAINS,    RENDER_CIV_ROME,     RENDER_GOD_NONE},
	{RENDER_BD_MAP_HIGHLANDS, RENDER_BD_BATTLE_HIGHLANDS, RENDER_CIV_ASSYRIA,  RENDER_GOD_NONE},
	{RENDER_BD_MAP_ARENA,     RENDER_BD_BATTLE_ARENA,     RENDER_CIV_CARTHAGE, RENDER_GOD_POSEIDON},
	{RENDER_BD_MAP_ARENA,     RENDER_BD_BATTLE_ARENA,     RENDER_CIV_ASSYRIA,  RENDER_GOD_KRONOS},
	{RENDER_BD_MAP_ARENA,     RENDER_BD_BATTLE_ARENA,     RENDER_CIV_ROME,     RENDER_GOD_HADES},
};

static const char *const GOD_NAMES[RENDER_GODS] = {
	"Zeus", "Poseidon", "Hades", "Ares", "Kronos",
};

const front_art_t *front_art(uint8_t province) {
	return &FRONTS[province & 7u];
}

const char *god_name(uint8_t god) {
	return (god < RENDER_GODS) ? GOD_NAMES[god] : "";
}

/* the newest verified slot as the row reports it (seq/day/treasury)    */
static int best_slot(const save_slot_info_t s[2]) {
	int best = -1;
	for (int i = 0; i < 2; ++i) {
		if (s[i].valid && (best < 0 || s[i].seq > s[best].seq)) {
			best = i;
		}
	}
	return best;
}

void save_row(uint8_t host, char *l1, char *l2, size_t n) {
	save_slot_info_t s[2];
	save_commit_info_t c;
	save_slots(s);
	save_last_commit(&c);
	const save_status_t st = save_status();
	const int best = best_slot(s);
	const bool blank = s[0].blank && s[1].blank;
	l1[0] = '\0';
	l2[0] = '\0';

	if (host == SAVE_ROW_SETTINGS && save_erase_pending()) {
		/* the line ui counts down lives in settings.c (5 s)          */
		snprintf(l1, n, "Erased %u B. Undo 5 s.", (unsigned)SAVE_CORE_BYTES);
		snprintf(l2, n, "A restores both slots.");
		return;
	}
	if (st == SAVE_STATUS_SAVING) {
		if (host == SAVE_ROW_TITLE) {
			snprintf(l1, n, "Checking save.");
			snprintf(l2, n, "Slot 1 of 2.");
		} else {
			snprintf(l1, n, "Saving 170 of 170 B.");
			snprintf(l2, n, "Verifying CRC.");
		}
		return;
	}
	if (st == SAVE_STATUS_NO_CARD) {
		if (host == SAVE_ROW_RESULT) {
			snprintf(l1, n, "Not saved. No card.");
			snprintf(l2, n, "Reward in memory only.");
		} else if (host == SAVE_ROW_SETTINGS) {
			snprintf(l1, n, "No save card.");
			snprintf(l2, n, "0 of 512 B written. Retry at next boundary.");
		} else {
			snprintf(l1, n, "No save card.");
			snprintf(l2, n, "Progress will not be kept.");
		}
		return;
	}
	if (st == SAVE_STATUS_FULL) {
		if (host == SAVE_ROW_RESULT) {
			snprintf(l1, n, "Card refused 2 writes.");
			snprintf(l2, n, "Reward in memory only.");
		} else if (best >= 0) {
			snprintf(l1, n, "Card refused 2 writes.");
			snprintf(l2, n, "Last good: Day %u, %lu.",
			         (unsigned)s[best].day,
			         (unsigned long)s[best].treasury);
		} else {
			snprintf(l1, n, "Save failed. Wrote 0 of 512 B.");
			snprintf(l2, n, "Press A to retry. Input kept.");
		}
		return;
	}
	if (st == SAVE_STATUS_VERIFY_FAIL) {
		if (host == SAVE_ROW_TITLE) {
			const unsigned bad = s[0].blank ? 2u : 1u;
			const unsigned other = 3u - bad;
			snprintf(l1, n, "Save data damaged.");
			snprintf(l2, n, "Slot %u unreadable, %u %s.", bad, other,
			         s[other - 1u].blank ? "blank" : "unreadable");
		} else {
			snprintf(l1, n, "Save data damaged.");
			snprintf(l2, n, "0 of 2 slots verified. %u B kept.",
			         (unsigned)SAVE_CORE_BYTES);
		}
		return;
	}
	/* OK */
	if (host == SAVE_ROW_RESULT) {
		if (c.tried && c.ok) {
			snprintf(l1, n, "Saved. Day %u, treasury %lu.",
			         (unsigned)c.day, (unsigned long)c.treasury);
			snprintf(l2, n, "Slot %u, seq %lu.", (unsigned)c.slot + 1u,
			         (unsigned long)c.seq);
		} else {
			snprintf(l1, n, "Not saved yet.");
			snprintf(l2, n, "Continue writes 170 B.");
		}
		return;
	}
	if (best < 0 || blank) {
		if (host == SAVE_ROW_TITLE) {
			snprintf(l1, n, "No save data.");
			snprintf(l2, n, "New: Day 1, treasury 300.");
		} else {
			snprintf(l1, n, "No campaign yet.");
			snprintf(l2, n, "0 of %u B used. Erase off.",
			         (unsigned)SAVE_CORE_BYTES);
		}
		return;
	}
	const int other = 1 - best;
	if (host == SAVE_ROW_TITLE && !s[other].valid && !s[other].blank) {
		snprintf(l1, n, "Slot %d damaged.", other + 1);
		snprintf(l2, n, "Using slot %d, seq %lu.", best + 1,
		         (unsigned long)s[best].seq);
		return;
	}
	if (host == SAVE_ROW_TITLE) {
		snprintf(l1, n, "Save ready.");
		snprintf(l2, n, "Slot %d, seq %lu, Day %u.", best + 1,
		         (unsigned long)s[best].seq, (unsigned)s[best].day);
		return;
	}
	snprintf(l1, n, "Saved. Day %u, treasury %lu.", (unsigned)s[best].day,
	         (unsigned long)s[best].treasury);
	snprintf(l2, n, "Slot %d, seq %lu, 170 B.", best + 1,
	         (unsigned long)s[best].seq);
}

/* the row as painted: a state pin (shape, never colour alone) + 2 lines */
void save_row_paint(render_screen_t scr, uint8_t host, int x, int y, int w) {
	char l1[64], l2[64];
	save_row(host, l1, l2, sizeof l1);
	static const uint8_t PIN[5] = {0u, 2u, 3u, 3u, 1u}; /* by status      */
	const save_status_t st = save_status();
	render_pin(scr, PIN[(unsigned)st <= 4u ? (unsigned)st : 0u], x, y,
	           RENDER_SPR_FRONT);
	const uint8_t c1 = (st == SAVE_STATUS_OK) ? RENDER_INK : RENDER_CRIT;
	render_text_wrap(scr, x + 20, y, w - 20, 1, c1, l1);
	render_text_wrap(scr, x + 20, y + 9, w - 20, 2, RENDER_INK2, l2);
}

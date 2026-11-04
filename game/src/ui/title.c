/* title.c - the title state machine (B07, spec-01 T008/T012-T018).
 * Walking-skeleton fidelity: copy strings and interaction are per spec-01
 * section 6; the 1x pixel layout and the 2 px ring component land with the
 * B27/B29 polish sweep. States: LOADING (skeleton while scan runs) ->
 * READY / PARTIAL / EMPTY / NO_CARD / BOTH_DAMAGED -> Campaign (handoff).
 * The economy tick stays frozen here (ADR-0001) and Continue never sits
 * on a damaged or empty card (spec-01 R5, spec-01 6.5).
 */
#include <nds.h>   /* libnds KEY_* mask; the host test mirrors the bits */
#include <stdio.h>
#include <string.h>

#include "contracts/save.h"
#include "contracts/sim_api.h"
#include "contracts/trace.h"
#include "contracts/ui.h"
#include "contracts/render.h"

#include "ui_draw.h"
#include "screens.h"
#include "title.h"

/* ---- state ------------------------------------------------------------- */
static title_state_t state;
static bool scan_pending;

static save_slot_info_t cards[2];

/* focus: 0 card1, 1 card2, 2 primary, 3 secondary (reading order, spec 6.7) */
static int focus;

/* ui.c owns the screen switch and the Campaign entry copy on this slot.  */
static int entered_slot = -1;

/* debounced tap 2 detection (spec-01 6.5: tap 1 rings, tap 2 acts)        */
static int last_tap_target = -1;
static bool awaiting_tap2;

/* ---- forward decls ------------------------------------------------------ */
static int default_focus_target(void);

/* ---- copy constants (spec-01 6.6) -------------------------------------- */
static const char TXT_EMPTY_HINT[] = "Recruit from HQ to open this front.";
static const char TXT_NO_CARD[]    = "No save card. Progress will not be kept.";
/* Copy block wraps at word boundaries (spec-01 6.2); BOTH_DAMAGED
 * (spec-01 6.6 T8) is drawn as 3 rows:
 * "Save data damaged. Both slots"
 * "unreadable. New Campaign"
 * "overwrites Slot 1 on first save."                                   */

/* ---- focus helpers ------------------------------------------------------- */
/* Selectable targets: a card only when its record is verified; the two
 * action buttons stay selectable. Damaged or empty cards stay visible but
 * out of the focus order (spec-01 6.5).                                   */
static bool selectable(int target) {
	if (target >= 0 && target < 2) {
		return cards[target].valid;
	}
	if (target == 2 || target == 3) {
		return true;
	}
	return false;
}

static void set_focus(int target) {
	if (selectable(target)) {
		focus = target;
		return;
	}
	/* never land on a damaged or empty card: fall back to the first
	 * selectable target in reading order                                   */
	for (int i = 0; i < 4; ++i) {
		if (selectable(i)) {
			focus = i;
			return;
		}
	}
	focus = 2; /* dead end: the primary stays reachable */
}

int title_default_slot(void) {
	int best = -1;
	for (int i = 0; i < 2; ++i) {
		if (!cards[i].valid) {
			continue;
		}
		/* highest seq wins; tie -> slot 1 (index order)                 */
		if (best < 0 || cards[i].seq > cards[best].seq) {
			best = i;
		}
	}
	return best;
}

/* The ring legend (spec-01 6.5): newest verified card if any, else the
 * primary action (NEW CAMPAIGN).                                          */
static int default_focus_target(void) {
	const int slot = title_default_slot();
	return (slot >= 0) ? slot : 2;
}

static int first_selectable_in_row(int row) {
	const int lo = (row == 0) ? 0 : 2; /* row 0 = cards, 1 = actions */
	const int hi = (row == 0) ? 1 : 3;
	for (int t = lo; t <= hi; ++t) {
		if (selectable(t)) {
			return t;
		}
	}
	return focus; /* nothing selectable in that row: stay put */
}

/* ---- state resolution (spec-01 6.4 / R1 / R3 / R4 / R5) ----------------- */
static void resolve_state(void) {
	save_slot_info_t info[2];
	save_slots(info);
	memcpy(cards, info, sizeof cards);

	if (save_status() == SAVE_STATUS_NO_CARD) {
		state = TITLE_NO_CARD;
	} else if (!cards[0].valid && !cards[1].valid) {
		/* Blank card, never written = Empty; an unreadable card with
		 * bytes still present = both slots damaged (spec-01 R5).      */
		state = (cards[0].blank && cards[1].blank) ? TITLE_EMPTY
		                                           : TITLE_BOTH_DAMAGED;
	} else if ((int)cards[0].valid + (int)cards[1].valid == 2) {
		state = TITLE_READY;
	} else {
		/* Exactly one verified slot; Partial when the other card was
		 * damaged, Ready when it simply was not written yet.          */
		const int ok = cards[0].valid ? 0 : 1;
		const int bad = 1 - ok;
		state = (!cards[bad].valid && !cards[bad].blank) ? TITLE_PARTIAL
		                                                 : TITLE_READY;
	}
	entered_slot = -1;
	set_focus(default_focus_target());
}

/* title_refresh(): re-read the cards after Settings changed the card
 * (Erase, Undo, Retry); the LOADING skeleton is not replayed.            */
void title_refresh(void) {
	if (state != TITLE_LOADING) {
		resolve_state();
	}
}

void title_init(void) {
	state = TITLE_LOADING;
	scan_pending = true;
	for (int i = 0; i < 2; ++i) {
		memset(&cards[i], 0, sizeof cards[i]);
	}
	focus = 2;
	entered_slot = -1;
	last_tap_target = -1;
	awaiting_tap2 = false;
}

title_state_t title_state(void) {
	return state;
}

int title_entered_slot(void) {
	return entered_slot;
}

/* ---- handoff (spec-01 R2, T014) ------------------------------------------ */
bool title_handoff(int slot) {
	if (slot < 0 || slot > 1) {
		return false; /* out of range: never a slot guess             */
	}
	save_slot_info_t info[2];
	save_slots(info);
	if (!info[slot].valid) {
		return false; /* Continue is never enabled on a corrupt slot   */
	}
	save_aggregate_t agg;
	if (save_load_slot(slot, &agg) != SAVE_STATUS_OK) {
		return false;
	}
	/* The title is a menu (ADR-0001): the run state enters with the
	 * tick counter frozen at 0 (spec-01 R2).                             */
	sim_ingest_save(&agg, info[slot].day);
	entered_slot = slot;
	state = TITLE_HANDOFF;
	/* B14: the title handoff hop, trace id = the slot's commit seq.      */
	trace_hop(TRACE_FLOW_TITLE, 1u, info[slot].seq);
	return true;
}

/* ---- input (spec-01 6.5) ------------------------------------------------- */
static int touch_target(uint32_t value) {
	const uint32_t px = (value >> 8) & 0xFFu;
	const uint32_t py = value & 0xFFu;
	/* frame_title geometry (art pass): 2 x 116 x 76 cards at y 30
	 * (x 8 / x 132), action row 116 x 20 at y 116 - every target >= 32 px
	 * tall with its 8 px apron (spec-01 6.2, constitution 7).            */
	if (py >= 26u && py < 110u) {
		if (px >= 4u && px < 128u) {
			return 0;
		}
		if (px >= 128u && px < 252u) {
			return 1;
		}
		return -1;
	}
	if (py >= 110u && py < 142u) {
		if (px >= 4u && px < 128u) {
			return 2; /* primary                                            */
		}
		if (px >= 128u && px < 252u) {
			return 3; /* secondary                                          */
		}
	}
	return -1;
}

static void clear_tap(void) {
	awaiting_tap2 = false;
	last_tap_target = -1;
}

static title_action_t act_on(int target) {
	if (target == 3) {
		return TITLE_ACT_SETTINGS; /* routed on-screen in B11 (spec-06) */
	}
	if (target == 2) {
		/* Primary: CONTINUE when any slot is verified; NEW CAMPAIGN
		 * only in Empty, NO_CARD, both-damaged (spec-01 6.5).          */
		const int d = title_default_slot();
		if (d >= 0) {
			return title_handoff(d) ? TITLE_ACT_CONFIRM_CARD
			                        : TITLE_ACT_NONE;
		}
		if (state == TITLE_EMPTY || state == TITLE_NO_CARD ||
		    state == TITLE_BOTH_DAMAGED) {
			/* New Campaign: Day 1 / treasury 300 (spec-01 fixures) */
			sim_init(1u);
			entered_slot = -1;
			state = TITLE_HANDOFF;
			return TITLE_ACT_NEW_ICON;
		}
		return TITLE_ACT_NONE;
	}
	/* A card: A / tap 2 loads the focused card                            */
	return title_handoff(target) ? TITLE_ACT_CONFIRM_CARD
	                             : TITLE_ACT_NONE;
}

title_action_t title_input(uint8_t kind, uint32_t value) {
	if (state == TITLE_LOADING) {
		return TITLE_ACT_NONE; /* input is queued: nothing interactive */
	}

	if (kind == (uint8_t)UI_EVT_KEY) {
		const uint32_t keys = value;
		if (keys & (KEY_UP | KEY_DOWN)) {
			/* D-pad U/D: between the card row and the action row.
			 * 1 target per press, never landing on a dead card.        */
			const int other_row = (focus < 2) ? 1 : 0;
			set_focus(first_selectable_in_row(other_row));
			clear_tap();
			return TITLE_ACT_NONE;
		}
		if (keys & (KEY_LEFT | KEY_RIGHT)) {
			/* linear reading order 0..3, skipping unselectable          */
			const int step = (keys & KEY_RIGHT) ? 1 : -1;
			for (int s = 1; s <= 3; ++s) {
				const int t = focus + s * step;
				if (t < 0 || t > 3) {
					break;
				}
				if (selectable(t)) {
					focus = t;
					break;
				}
			}
			clear_tap();
			return TITLE_ACT_NONE;
		}
		if (keys & KEY_B) {
			/* B: ring returns to the default card; never loads (6.5)   */
			set_focus(default_focus_target());
			clear_tap();
			return TITLE_ACT_NONE;
		}
		if (keys & KEY_START) {
			return TITLE_ACT_SETTINGS;
		}
		if (keys & KEY_A) {
			clear_tap();
			return act_on(focus);
		}
		return TITLE_ACT_NONE;
	}

	if (kind == (uint8_t)UI_EVT_TOUCH) {
		const int target = touch_target(value);
		if (target < 0) {
			return TITLE_ACT_NONE; /* outside every target */
		}
		if (target == 3) {
			return TITLE_ACT_SETTINGS;
		}
		if (!awaiting_tap2 || last_tap_target != target) {
			/* tap 1 (or a different target): ring moves only           */
			set_focus(target);
			awaiting_tap2 = true;
			last_tap_target = target;
			return TITLE_ACT_NONE;
		}
		/* tap 2 on the same target: same as A                       */
		clear_tap();
		return act_on(target);
	}
	return TITLE_ACT_NONE;
}

/* ---- draw (spec-01 6.2; text fidelity for the walking skeleton) -------- */
/* TODO B27/B31: swap the text console for the tokenized layout - the
 * focused-slot sheet (224 x 88), the two 128 x 32 cards and the 112 x 32
 * primary; the copy constants already match spec-01 6.6 byte-exactly.     */

static void draw_cards(char buf[80]) {
	for (int i = 0; i < 2; ++i) {
		if (cards[i].valid) {
			snprintf(buf, 80, "%sSLOT %u  DAY %u  TREASURY %lu",
			         focus == i ? ">" : " ", (unsigned)(i + 1u),
			         (unsigned)cards[i].day, (unsigned long)cards[i].treasury);
		} else if (cards[i].blank) {
			snprintf(buf, 80, " %sSLOT %u  EMPTY", focus == i ? ">" : " ",
			         (unsigned)(i + 1u));
		} else {
			/* T020: crit stamp ("!!" mark, non-colour) + text label.
			 * The tokenized 2 px stamp swaps in at B27; mark + label
			 * is the colour-independent signal for the damaged card. */
			snprintf(buf, 80, "%s!! SLOT %u  DAMAGED + UNREADABLE",
			         focus == i ? ">" : " ", (unsigned)(i + 1u));
		}
		ui_draw_text(0, 8 + i, buf);
	}
}

/* ---- art pass: the rows on the art, the cards and buttons painted ------- */
static const ui_row_t TITLE_ROWS[] = {
	{2, RENDER_TOP, 16, 10, 224, 1, RENDER_BRONZE_L},
	{3, RENDER_TOP, 16, 20, 224, 1, RENDER_LINE},
	{5, RENDER_BOTTOM, 104, 8, 148, 1, RENDER_LINE},
	{10, RENDER_BOTTOM, 8, 146, 240, 1, RENDER_INK},
	{14, RENDER_BOTTOM, 8, 146, 240, 2, RENDER_INK},
	{15, RENDER_BOTTOM, 8, 155, 240, 1, RENDER_INK},
	{16, RENDER_BOTTOM, 8, 164, 240, 1, RENDER_INK},
	{17, RENDER_BOTTOM, 6, 184, 248, 1, RENDER_LINE},
};

static void paint_card(int i, int x, int y) {
	char buf[24];
	const bool f = (focus == i);
	const uint32_t step = ui_anim_step();
	snprintf(buf, sizeof buf, "Slot %d", i + 1);
	if (cards[i].valid) {
		render_text(RENDER_BOTTOM, x + 10, y + 10,
		            f ? RENDER_ACCENT : RENDER_INK, buf);
		snprintf(buf, sizeof buf, "Day %u", (unsigned)cards[i].day);
		render_text(RENDER_BOTTOM, x + 64, y + 10, RENDER_INK2, buf);
		render_text(RENDER_BOTTOM, x + 10, y + 24, RENDER_INK2, "Treasury");
		snprintf(buf, sizeof buf, "%lu", (unsigned long)cards[i].treasury);
		render_text_big(RENDER_BOTTOM, x + 10, y + 34, RENDER_INK, buf);
		render_text(RENDER_BOTTOM, x + 10, y + 58, RENDER_INK2, "Hellenes");
		render_portrait(RENDER_BOTTOM, RENDER_CIV_HELLENES,
		                RENDER_CLS_SPEAR, RENDER_ANIM_IDLE, step + (unsigned)i,
		                x + 78, y + 50, RENDER_SPR_FRONT);
		render_portrait(RENDER_BOTTOM, RENDER_CIV_HELLENES,
		                RENDER_CLS_SPEAR, RENDER_ANIM_IDLE, step + 1u,
		                x + 92, y + 50, RENDER_SPR_FRONT);
		render_icon(RENDER_BOTTOM, RENDER_ICON_GOLD, x + 92, y + 32,
		            RENDER_SPR_FRONT);
	} else if (cards[i].blank) {
		render_text(RENDER_BOTTOM, x + 10, y + 10, RENDER_INK2, buf);
		render_text(RENDER_BOTTOM, x + 10, y + 28, RENDER_INK2, "Empty");
		render_text(RENDER_BOTTOM, x + 10, y + 40, RENDER_INK2,
		            "Never written.");
	} else {
		/* T020: the damaged stamp = pin SHAPE + word, never colour */
		render_text(RENDER_BOTTOM, x + 10, y + 10, RENDER_CRIT, buf);
		render_pin(RENDER_BOTTOM, 3u, x + 92, y + 6, RENDER_SPR_FRONT);
		render_text(RENDER_BOTTOM, x + 10, y + 28, RENDER_CRIT,
		            "!! DAMAGED");
		render_text(RENDER_BOTTOM, x + 10, y + 40, RENDER_INK2,
		            "Unreadable.");
		render_text(RENDER_BOTTOM, x + 10, y + 52, RENDER_INK2,
		            "Kept for Erase.");
	}
	if (f) {
		render_ring(RENDER_BOTTOM, x, y, 116, 76);
	}
}

static void paint(void) {
	render_backdrop(RENDER_TOP, RENDER_BD_TITLE_TOP, RENDER_GOD_NONE);
	render_backdrop(RENDER_BOTTOM, RENDER_BD_FRAME_TITLE, RENDER_GOD_NONE);
	/* top: the focused-slot sheet + the B24 SaveStatus row             */
	render_panel(RENDER_TOP, 8, 4, 240, 50, RENDER_PANEL_IRON);
	save_row_paint(RENDER_TOP, SAVE_ROW_TITLE, 16, 31, 224);
	render_text(RENDER_BOTTOM, 8, 8, RENDER_BRONZE_L, "FIELD LEDGER");
	if (state == TITLE_LOADING) {
		return;
	}
	for (int i = 0; i < 2; ++i) {
		paint_card(i, 8 + i * 124, 30);
	}
	const bool cont = title_default_slot() >= 0;
	render_button(RENDER_BOTTOM, 8, 116, 116, 20,
	              cont ? "Continue" : "New Campaign",
	              focus == 2 ? RENDER_BTN_FOCUS : RENDER_BTN_NORMAL);
	render_button(RENDER_BOTTOM, 132, 116, 116, 20, "Settings",
	              focus == 3 ? RENDER_BTN_FOCUS : RENDER_BTN_NORMAL);
	if (focus >= 2) {
		render_ring(RENDER_BOTTOM, focus == 2 ? 8 : 132, 116, 116, 20);
	}
}

void title_frame(void) {
	char buf[80];

	ui_draw_layout(TITLE_ROWS, (uint8_t)(sizeof TITLE_ROWS / sizeof TITLE_ROWS[0]));
	paint();

	/* The scan runs between frames so the skeleton is shown first (R8);
	 * it is read-only: the title never writes to the card (spec-01).       */
	if (scan_pending) {
		ui_draw_text(0, 0, "WAR TABLE");
		ui_draw_text(0, 2, "Reading save card 1 of 2. 250 ms max.");
		ui_draw_text(0, 8, "[.......]   [.......]");
		ui_draw_text(0, 10, "LOADING...");
		scan_pending = false;
		save_init(); /* caps the scan at 2 x 250 ms (spec-01 R8) */
		resolve_state();
		/* B14 (spec-01 T011): one line per scan over both slots, the
		 * trace id = slot commit seq; 0 telemetry egress - this mirrors
		 * what the L+R+START overlay shows on device.                  */
		trace_hop(TRACE_FLOW_BOOT_SCAN, 1u, cards[0].seq);
		trace_hop(TRACE_FLOW_BOOT_SCAN, 2u, cards[1].seq);
		iprintf("t=title ev=scan slot=1 seq=%lu v=%u "
		        "slot=2 seq=%lu v=%u\n",
		        (unsigned long)cards[0].seq, (unsigned)cards[0].valid,
		        (unsigned long)cards[1].seq, (unsigned)cards[1].valid);
		return;
	}

	/* TOP panel: focused-slot sheet                                       */
	if (state == TITLE_EMPTY) {
		ui_draw_text(0, 2, "NO CAMPAIGN STORED");
		snprintf(buf, sizeof buf, "TREASURY ");
		ui_draw_text(0, 3, buf);
	} else if (state == TITLE_NO_CARD) {
		ui_draw_text(0, 2, "NO SAVE CARD");
	} else if (state == TITLE_BOTH_DAMAGED) {
		ui_draw_text(0, 2, "NO VERIFIED SAVE");
	} else {
		const int d = title_default_slot();
		if (d >= 0) {
			snprintf(buf, sizeof buf, "SLOT %u  DAY %u",
			         (unsigned)(d + 1u), (unsigned)cards[d].day);
			ui_draw_text(0, 2, buf);
			snprintf(buf, sizeof buf, "TREASURY %lu",
			         (unsigned long)cards[d].treasury);
			ui_draw_text(0, 3, buf);
		}
	}

	/* scan status line                                                    */
	snprintf(buf, sizeof buf, "%s / %u OF 2 SLOTS",
	         save_status_text(save_status()),
	         (unsigned)((int)cards[0].valid + (int)cards[1].valid));
	ui_draw_text(0, 5, buf);
	ui_draw_text(0, 6, "ECONOMY PAUSED / TICK 0");

	/* BOTTOM panel: 2 slot cards + action row                             */
	draw_cards(buf);
	snprintf(buf, sizeof buf, "[%s]%s", title_default_slot() >= 0
	                                           ? "CONTINUE"
	                                           : "NEW CAMPAIGN",
	         focus == 2 ? "<" : " ");
	ui_draw_text(0, 11, buf);
	ui_draw_text(0, 12, "[ SETTINGS ]");

	/* copy block (per state, byte-exact constants)                        */
	switch (state) {
	case TITLE_EMPTY:
		ui_draw_text(0, 14, TXT_EMPTY_HINT);
		break;
	case TITLE_NO_CARD:
		ui_draw_text(0, 14, TXT_NO_CARD);
		break;
	case TITLE_BOTH_DAMAGED: {
		/* word-wrapped at the fixed width (spec-01 6.2)               */
		ui_draw_text(0, 14, "Save data damaged. Both slots");
		ui_draw_text(0, 15, "unreadable. New Campaign");
		ui_draw_text(0, 16, "overwrites Slot 1 on first save.");
		break;
	}
	case TITLE_PARTIAL: {
		const int ok = cards[0].valid ? 0 : 1;
		const int bad = 1 - ok;
		snprintf(buf, sizeof buf,
		         "Slot %u damaged. Continuing from Slot %u, Day %u.",
		         (unsigned)(bad + 1u), (unsigned)(ok + 1u),
		         (unsigned)cards[ok].day);
		ui_draw_text(0, 14, buf);
		break;
	}
	case TITLE_READY: {
		const int d = title_default_slot();
		snprintf(buf, sizeof buf, "Slot %u verified. Day %u, treasury %lu.",
		         (unsigned)(d + 1u), (unsigned)cards[d].day,
		         (unsigned long)cards[d].treasury);
		ui_draw_text(0, 14, buf);
		break;
	}
	case TITLE_LOADING:
	case TITLE_HANDOFF:
		break;
	}

	ui_draw_text(0, 17, "D-PAD MOVE / A CONFIRM / START SETTINGS");
}
